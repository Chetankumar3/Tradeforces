"""Worker service for VM creation."""

import asyncio
import json
import logging
from typing import Optional, Dict, Any
import google.auth
from google.cloud import pubsub_v1
from google.cloud.devtools import cloudbuild_v1
from google.cloud import storage
from kubernetes import client, config, watch
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy import select, update
import sys
import os
from jinja2 import Template

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from shared_core.DB_models import Submission
from shared_core.schema_mapper import SchemaMapper
from shared_core.core import create_async_db_engine, async_sessionmaker
from .config import settings

logger = logging.getLogger(__name__)
schema_mapper = SchemaMapper()


class VMCreatorWorker:
    """Worker for creating microVMs from submissions."""
    
    def __init__(self):
        """Initialize worker."""
        self.curr_pending_microvms = 0
        self.credentials, self.project_id = google.auth.default(
            scopes=["https://www.googleapis.com/auth/cloud-platform"]
        )
        self.publisher = pubsub_v1.PublisherClient(credentials=self.credentials)
        self.subscriber = pubsub_v1.SubscriberClient(credentials=self.credentials)
        self.cloudbuild_client = cloudbuild_v1.CloudBuildClient(credentials=self.credentials)
        self.storage_client = storage.Client(project=self.project_id, credentials=self.credentials)
        
        # Load K3s config
        try:
            config.load_incluster_config()
        except Exception:
            config.load_kube_config()  # Fallback for local testing
        
        self.k8s_client = client.CoreV1Api()
        self.k8s_apps_client = client.AppsV1Api()
        
        # Load Pod template
        self.pod_template = self._load_pod_template()
        
        # Database setup
        self.engine = create_async_db_engine()
        self.AsyncSessionLocal = async_sessionmaker(
            self.engine, class_=AsyncSession, expire_on_commit=False
        )
    
    def _load_pod_template(self) -> str:
        """Load the multi-resource pod template used for VM creation."""
        template_path = os.path.join(os.path.dirname(__file__), "create_3_pods.tmpl")
        try:
            with open(template_path, "r", encoding="utf-8") as f:
                content = f.read()
                logger.info(f"Loaded pod template from {template_path}")
                return content
        except FileNotFoundError:
            logger.warning(
                f"Pod template not found at {template_path}; falling back to built-in default"
            )
            return self._get_default_pod_template()

        def _get_default_pod_template(self) -> str:
            """Get default Pod template if file not found."""
            return """
apiVersion: v1
kind: Pod
metadata:
  name: {{ submission_id }}
  namespace: {{ namespace }}
  labels:
    submission-id: "{{ submission_id }}"
    user-id: "{{ user_id }}"
spec:
  runtimeClassName: kata
  containers:
  - name: trader
    image: {{ docker_image }}
    imagePullPolicy: IfNotPresent
    resources:
      requests:
        memory: "256Mi"
        cpu: "100m"
      limits:
        memory: "512Mi"
        cpu: "500m"
  restartPolicy: Never
"""
    
    async def trigger_cloud_build(
        self,
        gcs_url: str,
        submission_id: int,
        user_id: int
    ) -> str:
        """Trigger Cloud Build to build and push image."""
        # Parse gcs_url (gs://<bucket>/<object>) — use the actual stored path, not a derived one
        if gcs_url.startswith("gs://"):
            without_scheme = gcs_url[5:]
            slash_idx = without_scheme.index("/")
            src_bucket = without_scheme[:slash_idx]
            src_object = without_scheme[slash_idx + 1:]
        else:
            src_bucket = settings.gcs_bucket_name
            src_object = f"submissions/{user_id}/{submission_id}.zip"
            logger.warning(
                f"[CB] submission={submission_id} | gcs_url has unexpected format '{gcs_url}'; "
                f"falling back to gs://{src_bucket}/{src_object}"
            )
    
        image_uri = f"{settings.artifact_registry_url}/submission-{submission_id}:latest"
        logger.info(
            f"[CB] submission={submission_id} user={user_id} | "
            f"source=gs://{src_bucket}/{src_object} → {image_uri}"
        )
    
        build = cloudbuild_v1.Build(
            service_account=(
                f"projects/{self.project_id}/serviceAccounts/"
                f"{settings.cloud_build_service_account}"
            ),
            options=cloudbuild_v1.BuildOptions(
                default_logs_bucket_behavior=(
                    cloudbuild_v1.BuildOptions.DefaultLogsBucketBehavior.   REGIONAL_USER_OWNED_BUCKET
                )
            ),
            source=cloudbuild_v1.Source(
                storage_source=cloudbuild_v1.StorageSource(
                    bucket=src_bucket,
                    object_=src_object,
                )
            ),
            steps=[
                cloudbuild_v1.BuildStep(
                    name="gcr.io/cloud-builders/docker",
                    entrypoint="bash",
                    args=[
                        "-c",
                        (
                            "DOCKER_BUILDKIT=1 DOCKER_CTX=$(dirname $(find /workspace -name Dockerfile -maxdepth 2 | head -1)) && "
                            "echo \"[CB] Building from context: $$DOCKER_CTX\" && "
                            f"docker buildx build --load --tag {image_uri} $$DOCKER_CTX"
                        ),
                    ],
                ),
                cloudbuild_v1.BuildStep(
                    name="gcr.io/cloud-builders/docker",
                    args=["push", image_uri],
                ),
            ],
            images=[image_uri],
        )
    
        operation = self.cloudbuild_client.create_build(
            request=cloudbuild_v1.CreateBuildRequest(
                project_id=self.project_id,
                build=build,
            )
        )
        logger.info(f"[CB] submission={submission_id} | operation started: {operation.operation.name}")
    
        try:
            completed_build = await asyncio.to_thread(lambda: operation.result(timeout=600))
        except Exception as exc:
            logger.error(f"[CB] submission={submission_id} | operation.result() raised: {exc}")
            raise
    
        if completed_build.status != cloudbuild_v1.Build.Status.SUCCESS:
            logger.error(
                f"[CB] submission={submission_id} | terminal status={completed_build.status.name} "
                f"failure_msg={completed_build.failure_message}"
            )
            raise RuntimeError(
                f"Cloud Build failed [{completed_build.status.name}]: {completed_build.failure_message}"
            )
    
        logger.info(f"[CB] submission={submission_id} | build succeeded → {image_uri}")
        return image_uri
    
    def _build_render_context(
        self,
        submission_id: int,
        user_id: int,
        docker_image: str,
        namespace: str,
        pod_ip: Optional[str],
        topic_name: str,
    ) -> Dict[str, Any]:
        """Build the Jinja render context used by the manifest template."""
        return {
            "submission_id": submission_id,
            "user_id": user_id,
            "docker_image": docker_image,
            "namespace": namespace,
            "node_name": "",
            "contestant_ingress_addr": pod_ip or "",
            "contestant_egress_addr": pod_ip or "",
            "shadow_egress_addr": pod_ip or "",
            "redpanda_orders_topic": topic_name,
        }

    async def deploy_pod(
        self,
        submission_id: int,
        user_id: int,
        docker_image: str,
        topic_name: Optional[str] = None,
    ) -> Dict[str, Any]:
        """Deploy the microVM pod and the telemetry/shadow deployments on the same node."""
        logger.info(f"Deploying VM resources for submission {submission_id}")

        import yaml

        topic_name = topic_name or str(submission_id)

        template = Template(self.pod_template)
        rendered_manifest = template.render(
            **self._build_render_context(
                submission_id=submission_id,
                user_id=user_id,
                docker_image=docker_image,
                namespace=settings.k3s_namespace,
                pod_ip=None,
                topic_name=topic_name,
            )
        )

        resources = list(yaml.safe_load_all(rendered_manifest))
        if not resources:
            raise ValueError("No Kubernetes resources were rendered from the template")

        microvm_manifest = resources[0]
        pod = self.k8s_client.create_namespaced_pod(
            namespace=settings.k3s_namespace,
            body=microvm_manifest
        )
        logger.info(f"MicroVM pod created: {pod.metadata.name}")

        pod_ip = await self._wait_for_pod_running(pod.metadata.name)

        rendered_manifest = Template(self.pod_template).render(
            **self._build_render_context(
                submission_id=submission_id,
                user_id=user_id,
                docker_image=docker_image,
                namespace=settings.k3s_namespace,
                pod_ip=pod_ip,
                topic_name=topic_name,
            )
        )

        resources = list(yaml.safe_load_all(rendered_manifest))
        if not resources:
            raise ValueError("No Kubernetes resources were rendered from the template")

        microvm_manifest = resources[0]

        pod_details = self.k8s_client.read_namespaced_pod(
            name=pod.metadata.name,
            namespace=settings.k3s_namespace
        )
        node_name = pod_details.spec.node_name or pod_details.status.host_ip
        if not node_name:
            raise RuntimeError("Unable to resolve the node name for the microVM pod")

        for resource in resources[1:]:
            kind = resource.get("kind", "").lower()
            if kind == "deployment":
                spec = resource.setdefault("spec", {})
                template_spec = spec.setdefault("template", {}).setdefault("spec", {})
                template_spec.setdefault("nodeSelector", {})["kubernetes.io/hostname"] = node_name
                self.k8s_apps_client.create_namespaced_deployment(
                    namespace=settings.k3s_namespace,
                    body=resource
                )
                logger.info(f"Created deployment {resource['metadata']['name']} on node {node_name}")
            elif kind == "pod":
                spec = resource.setdefault("spec", {})
                spec.setdefault("nodeSelector", {})["kubernetes.io/hostname"] = node_name
                self.k8s_client.create_namespaced_pod(
                    namespace=settings.k3s_namespace,
                    body=resource
                )
                logger.info(f"Created pod {resource['metadata']['name']} on node {node_name}")

        microvm_name = microvm_manifest.get("metadata", {}).get("name") or f"microvm-{submission_id}"
        telemetry_name = resources[1].get("metadata", {}).get("name") if len(resources) > 1 else f"telemetry-deployment-{submission_id}"
        shadow_name = resources[2].get("metadata", {}).get("name") if len(resources) > 2 else f"shadow-deployment-{submission_id}"

        return {
            "pod_name": pod.metadata.name,
            "pod_ip": pod_ip,
            "node_name": node_name,
            "microvm_pod_name": pod.metadata.name,
            "telemetry_pod_name": resources[1]['metadata']['name'] if len(resources) > 1 else None,
            "shadow_pod_name": resources[2]['metadata']['name'] if len(resources) > 2 else None,
        }
    
    async def _wait_for_pod_running(self, pod_name: str) -> str:
        """Wait for Pod to reach Running phase and extract IP."""
        logger.info(f"Waiting for Pod {pod_name} to reach Running phase")
        
        max_wait = 300  # 5 minutes
        start_time = asyncio.get_event_loop().time()
        
        while True:
            try:
                pod = self.k8s_client.read_namespaced_pod(
                    name=pod_name,
                    namespace=settings.k3s_namespace
                )
                
                phase = getattr(pod.status, "phase", None)
                pod_ip = getattr(pod.status, "pod_ip", None)

                if phase == "Running" and pod_ip:
                    logger.info(f"Pod {pod_name} is running with IP: {pod_ip}")
                    return pod_ip

                if phase in ["Running", "Pending"] and not pod_ip:
                    elapsed = asyncio.get_event_loop().time() - start_time
                    if elapsed > max_wait:
                        raise TimeoutError(
                            f"Pod {pod_name} did not receive an IP address within {max_wait}s"
                        )
                    await asyncio.sleep(2)
                    continue

                if phase in ["Failed", "Unknown"]:
                    raise Exception(f"Pod failed with phase: {phase}")
                
                elapsed = asyncio.get_event_loop().time() - start_time
                if elapsed > max_wait:
                    raise Exception(f"Pod did not reach Running phase within {max_wait}s")
                
                await asyncio.sleep(2)
            except Exception as e:
                logger.error(f"Error waiting for Pod: {e}")
                raise
    
    async def create_redpanda_topic(self, topic_name: str) -> None:
        """Create the Redpanda topic for this submission."""
        if not settings.redpanda_bootstrap_servers:
            logger.warning("[RP] REDPANDA_BOOTSTRAP_SERVERS not set; skipping topic creation")
            return
    
        try:
            from confluent_kafka import KafkaException
            from confluent_kafka.admin import AdminClient, NewTopic
        except ImportError as exc:
            logger.error(f"[RP] confluent-kafka not installed: {exc}")
            raise

        logger.info(
            f"[RP] Connecting to {settings.redpanda_bootstrap_servers} "
            f"as {settings.redpanda_sasl_username}"
        )

        conf = {
            "bootstrap.servers": ",".join(
                s.strip() for s in settings.redpanda_bootstrap_servers.split(",") if s.strip()
            ),
            "security.protocol": "SASL_SSL",
            "sasl.mechanisms": settings.redpanda_sasl_mechanism,
            "sasl.username": settings.redpanda_sasl_username,
            "sasl.password": settings.redpanda_sasl_password,
        }
        admin = AdminClient(conf)

        try:
            futures = admin.create_topics([
                NewTopic(
                    topic_name,
                    num_partitions=1,
                    replication_factor=3,
                )
            ])

            for created_topic, future in futures.items():
                try:
                    future.result()
                    logger.info(
                        f"[RP] Created topic '{created_topic}' (1 partition, cluster-default replication)"
                    )
                except KafkaException as exc:
                    error_code = exc.args[0].name() if hasattr(exc.args[0], "name") else exc.args[0]
                    if error_code == "TOPIC_ALREADY_EXISTS":
                        logger.info(f"[RP] Topic '{created_topic}' already exists; skipping")
                    else:
                        logger.error(f"[RP] Failed to create topic '{created_topic}': {exc}")
                        raise
                except Exception as exc:
                    logger.error(f"[RP] Failed to create topic '{created_topic}': {exc}")
                    raise
        except Exception as exc:
            logger.error(f"[RP] Failed to create topic '{topic_name}': {exc}")
            raise
        finally:
            admin.close()

    def _queue2_backlog(self) -> int:
       """Return the current Queue 2 undelivered message count via Cloud Monitoring."""
       try:
           from google.cloud import monitoring_v3
           import time as _time

           mc = monitoring_v3.MetricServiceClient(credentials=self.credentials)
           now = _time.time()
           interval = monitoring_v3.TimeInterval(
               end_time={"seconds": int(now)},
               start_time={"seconds": int(now) - 120},
           )
           results = mc.list_time_series(
               request={
                   "name": f"projects/{self.project_id}",
                   "filter": (
                       'metric.type="pubsub.googleapis.com/subscription/num_undelivered_messages"'
                       f' AND resource.labels.subscription_id="{settings.queue2_subscription_name}"'
                   ),
                   "interval": interval,
                   "view": monitoring_v3.ListTimeSeriesRequest.TimeSeriesView.FULL,
               }
           )
           latest = 0
           for ts in results:
               for point in ts.points:
                   latest = max(latest, point.value.int64_value)
           logger.debug(f"[BP] Queue 2 undelivered={latest} (threshold={settings.max_queue2_size})")
           return latest
       except Exception as exc:
           logger.warning(f"[BP] Unable to read Queue 2 backlog via Monitoring API: {exc}; assuming 0")
           return 0

    async def publish_to_queue2(
        self,
        submission_id: int,
        user_id: int,
       microvm_pod_name: str,
        telemetry_pod_name: str,
        shadow_pod_name: str,
        topic_name: str,
    ) -> None:
        """Publish the updated Queue 2 payload for the submission."""
        message_data = {
            "submission_id": submission_id,
            "user_id": user_id,
            "microvm_pod-name": microvm_pod_name,
            "telemetry_pod-name": telemetry_pod_name,
            "shadow_pod-name": shadow_pod_name,
            "topic_name": topic_name,
        }

        queue2_message = schema_mapper.to_queue2(message_data)
        topic_path = self.publisher.topic_path(self.project_id, settings.queue2_name)
        payload = json.dumps(queue2_message).encode("utf-8")

        logger.info(
            f"[Q2] submission={submission_id} | publishing to {topic_path} | "
            f"microvm={microvm_pod_name} telemetry={telemetry_pod_name} shadow={shadow_pod_name}"
        )

        future = self.publisher.publish(topic_path, payload)
        try:
            message_id = await asyncio.to_thread(future.result)
            logger.info(f"[Q2] submission={submission_id} | published message_id={message_id}")
        except Exception as exc:
            logger.error(f"[Q2] submission={submission_id} | publish failed: {exc}")
            raise
    
    async def process_submission(
        self,
        message: pubsub_v1.types.PubsubMessage,
        lease_extension_task: asyncio.Task
    ) -> bool:
        """Process a single submission message."""
        try:
            # Parse message
            message_data = json.loads(message.data.decode("utf-8"))
            internal_data = schema_mapper.from_queue1(message_data)
            
            submission_id = internal_data.get("submission_id")
            user_id = internal_data.get("user_id")
            gcs_url = internal_data.get("gcs_url")
            
            logger.info(f"Processing submission {submission_id} for user {user_id}")
            
            # Trigger Cloud Build
            docker_image = await self.trigger_cloud_build(gcs_url, submission_id, user_id)
            logger.info(f"Cloud Build completed for submission {submission_id}, image: {docker_image}")

            topic_name = str(submission_id)

            # Create the Redpanda topic before the telemetry/shadow manifest is rendered.
            await self.create_redpanda_topic(topic_name)
            logger.info(f"Redpanda topic '{topic_name}' is ready for submission {submission_id}")

            # Deploy the VM, telemetry, and shadow resources using the topic name in the env vars.
            deployment_info = await self.deploy_pod(
                submission_id,
                user_id,
                docker_image,
                topic_name=topic_name,
            )
            logger.info(f"Deployment info for submission {submission_id}: {deployment_info}")

            # Publish the updated Queue 2 payload
            await self.publish_to_queue2(
                submission_id,
                user_id,
                deployment_info.get("microvm_pod_name") or "",
                deployment_info.get("telemetry_pod_name") or "",
                deployment_info.get("shadow_pod_name") or "",
                topic_name,
            )
            
            # Update DB status
            async with self.AsyncSessionLocal() as session:
                await session.execute(
                    update(Submission)
                    .where(Submission.id == submission_id)
                    .values(status="queued_for_loadgen_setup")
                )
                await session.commit()
            
            # Only ACK after successful completion
            message.ack()
            logger.info(f"Submission {submission_id} processed successfully")
            
            self.curr_pending_microvms -= 1
            return True
        
        except Exception as e:
            logger.error(f"Error processing message: {e}")
            # NACK message (will be redelivered)
            message.nack()
            self.curr_pending_microvms -= 1
            return False
    
    async def lease_extension_loop(
        self,
        message: pubsub_v1.types.PubsubMessage,
        cancel_event: asyncio.Event
    ) -> None:
        """Extend message lease periodically."""
        while not cancel_event.is_set():
            try:
                await asyncio.sleep(settings.reset_ttl)
                # Extend lease (Pub/Sub client handles this internally)
                logger.debug(f"Extended lease for message")
            except asyncio.CancelledError:
                break
            except Exception as e:
                logger.error(f"Error extending lease: {e}")
    
    async def message_callback(
        self,
        message: pubsub_v1.types.PubsubMessage
    ) -> None:
        """Callback for received messages."""
        # Backpressure check: do not pull from queue1 when Queue 2 already exceeds the configured limit
        if self._queue2_backlog() >= settings.max_queue2_size:
            logger.warning("Queue 2 is above MAX_QUEUE2_SIZE; deferring message for redelivery")
            message.nack()
            return

        # Check if we can accept more pending VMs
        if self.curr_pending_microvms >= settings.max_pending_microvms:
            logger.warning("Max pending VMs reached, NACK message for redelivery")
            message.nack()
            return
        
        self.curr_pending_microvms += 1
        
        # Create lease extension task
        cancel_event = asyncio.Event()
        lease_task = asyncio.create_task(
            self.lease_extension_loop(message, cancel_event)
        )
        
        try:
            # Process submission
            await self.process_submission(message, lease_task)
        finally:
            cancel_event.set()
            lease_task.cancel()
            try:
                await lease_task
            except asyncio.CancelledError:
                pass
    
    async def start_listener(self) -> None:
        """Start listening for messages."""
        logger.info(f"Starting VM Creator worker for subscription: {settings.queue1_subscription_name}")

        subscription_path = self.subscriber.subscription_path(
            self.project_id,
            settings.queue1_subscription_name
        )

        loop = asyncio.get_running_loop()
        logger.debug("Successfully captured main asyncio event loop.")

        def threadsafe_callback(msg):
            logger.info(f"Background thread received msg_id: {msg.message_id}. Injecting into event loop...")
            asyncio.run_coroutine_threadsafe(self.message_callback(msg), loop)

        logger.info(f"Opening gRPC streaming pull connection to {subscription_path}...")

        # Store on self so it can be gracefully cancelled during app teardown
        self.streaming_pull_future = self.subscriber.subscribe(
            subscription_path,
            callback=threadsafe_callback
        )

        logger.info("Streaming pull connection established. Waiting for messages...")

        try:
            await asyncio.wrap_future(self.streaming_pull_future)
        except asyncio.CancelledError:
            logger.info("Listener task was cancelled (likely application shutdown).")
            self.streaming_pull_future.cancel()
        except Exception as e:
            logger.error(f"Listener stream encountered a fatal error: {e}", exc_info=True)
            self.streaming_pull_future.cancel()


async def run_worker() -> None:
    """Run the VM creator worker."""
    worker = VMCreatorWorker()
    await worker.start_listener()
