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
                return f.read()
        except FileNotFoundError:
            logger.warning(f"Pod template not found at {template_path}, falling back to legacy template")
            legacy_template = os.path.join(os.path.dirname(__file__), "create_3_pods.tmpl")
            with open(legacy_template, "r", encoding="utf-8") as f:
                return f.read()
    
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
        logger.info(f"Triggering Cloud Build for submission {submission_id}")
        
        # Cloud Build step to unzip, build, and push
        build_config = {
            "name": f"submission-{submission_id}",
            "source": {
                "storage_source": {
                    "bucket": settings.gcs_bucket_name,
                    "object": f"submissions/{user_id}/{submission_id}.zip"
                }
            },
            "steps": [
                {
                    "name": "gcr.io/cloud-builders/gke-deploy",
                    "args": ["run", "--filename=Dockerfile"],
                },
                {
                    "name": "gcr.io/cloud-builders/docker",
                    "args": [
                        "build",
                        "-t",
                        f"{settings.artifact_registry_url}/submission-{submission_id}:latest",
                        "."
                    ]
                },
                {
                    "name": "gcr.io/cloud-builders/docker",
                    "args": [
                        "push",
                        f"{settings.artifact_registry_url}/submission-{submission_id}:latest"
                    ]
                }
            ],
            "images": [
                f"{settings.artifact_registry_url}/submission-{submission_id}:latest"
            ]
        }
        
        # Create build
        build_request = cloudbuild_v1.CreateBuildRequest(
            project_id=self.project_id,
            build=build_config
        )
        
        operation = self.cloudbuild_client.create_build(request=build_request)
        logger.info(f"Build started: {operation.name}")
        
        # Wait for build to complete
        import time
        while True:
            build = self.cloudbuild_client.get_build(
                project_id=self.project_id,
                id=operation.name.split("/")[-1]
            )
            
            if build.status in [cloudbuild_v1.Build.Status.SUCCESS]:
                logger.info(f"Build succeeded for submission {submission_id}")
                return f"{settings.artifact_registry_url}/submission-{submission_id}:latest"
            elif build.status in [cloudbuild_v1.Build.Status.FAILURE, cloudbuild_v1.Build.Status.TIMEOUT]:
                raise Exception(f"Build failed: {build.failure_message}")
            
            await asyncio.sleep(5)
    
    async def deploy_pod(
        self,
        submission_id: int,
        user_id: int,
        docker_image: str
    ) -> Dict[str, Any]:
        """Deploy the microVM pod and the telemetry/shadow deployments on the same node."""
        logger.info(f"Deploying VM resources for submission {submission_id}")

        import yaml

        template = Template(self.pod_template)
        rendered_manifest = template.render(
            submission_id=submission_id,
            user_id=user_id,
            docker_image=docker_image,
            namespace=settings.k3s_namespace,
            node_name=""
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

        pod_details = self.k8s_client.read_namespaced_pod(
            name=pod.metadata.name,
            namespace=settings.k3s_namespace
        )
        node_name = pod_details.spec.node_name or pod_details.status.host_ip
        if not node_name:
            raise RuntimeError("Unable to resolve the node name for the microVM pod")

        for resource in resources[1:]:
            if resource.get("kind", "").lower() == "deployment":
                spec = resource.setdefault("spec", {})
                template_spec = spec.setdefault("template", {}).setdefault("spec", {})
                template_spec.setdefault("nodeSelector", {})["kubernetes.io/hostname"] = node_name
                self.k8s_apps_client.create_namespaced_deployment(
                    namespace=settings.k3s_namespace,
                    body=resource
                )
                logger.info(f"Created deployment {resource['metadata']['name']} on node {node_name}")

        return {
            "pod_name": pod.metadata.name,
            "pod_ip": pod_ip,
            "node_name": node_name,
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
                
                if pod.status.phase == "Running":
                    pod_ip = pod.status.pod_ip
                    logger.info(f"Pod {pod_name} is running with IP: {pod_ip}")
                    return pod_ip
                
                if pod.status.phase in ["Failed", "Unknown"]:
                    raise Exception(f"Pod failed with phase: {pod.status.phase}")
                
                elapsed = asyncio.get_event_loop().time() - start_time
                if elapsed > max_wait:
                    raise Exception(f"Pod did not reach Running phase within {max_wait}s")
                
                await asyncio.sleep(2)
            except Exception as e:
                logger.error(f"Error waiting for Pod: {e}")
                raise
    
    async def create_redpanda_topic(self, topic_name: str) -> None:
        """Create the Redpanda topic synchronously for this submission."""
        if not settings.redpanda_bootstrap_servers:
            logger.warning("REDPANDA_BOOTSTRAP_SERVERS is not configured; skipping topic creation")
            return

        try:
            from kafka import KafkaAdminClient
            from kafka.admin import NewTopic
        except ImportError as exc:
            logger.warning(f"Kafka client dependency is unavailable: {exc}")
            return

        admin_client = KafkaAdminClient(
            bootstrap_servers=[server.strip() for server in settings.redpanda_bootstrap_servers.split(',') if server.strip()],
            client_id="tradeforces-vm-creator",
            security_protocol="SASL_SSL" if settings.redpanda_sasl_username and settings.redpanda_sasl_password else "SSL",
            ssl_check_hostname=True,
            sasl_mechanism=settings.redpanda_sasl_mechanism,
            sasl_plain_username=settings.redpanda_sasl_username,
            sasl_plain_password=settings.redpanda_sasl_password,
        )
        try:
            admin_client.create_topics([NewTopic(name=topic_name, num_partitions=1, replication_factor=1)])
            logger.info(f"Created Redpanda topic {topic_name} with 1 partition")
        except Exception as exc:
            logger.warning(f"Skipping Redpanda topic creation for {topic_name}: {exc}")
        finally:
            admin_client.close()

    def _queue2_backlog(self) -> int:
        """Return the current Queue 2 backlog for backpressure monitoring."""
        try:
            subscription_path = self.subscriber.subscription_path(self.project_id, settings.queue2_subscription_name)
            subscription = self.subscriber.get_subscription(request={"subscription": subscription_path})
            return int(getattr(subscription, "num_messages", 0) or 0)
        except Exception as exc:
            logger.warning(f"Unable to read Queue 2 backlog: {exc}")
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
        logger.info(f"Publishing result for submission {submission_id} to Queue 2")

        message_data = {
            "submission_id": submission_id,
            "user_id": user_id,
            "microvm_pod-name": microvm_pod_name,
            "telemetry_pod-name": telemetry_pod_name,
            "shadow_pod-name": shadow_pod_name,
            "topic_name": topic_name,
        }
        
        # Map using schema
        queue2_message = schema_mapper.to_queue2(message_data)
        
        topic_path = self.publisher.topic_path(self.project_id, settings.queue2_name)
        self.publisher.publish(
            topic_path,
            json.dumps(queue2_message).encode("utf-8")
        )
        logger.info(f"Message published to Queue 2 for submission {submission_id}")
    
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

            # Deploy the VM, telemetry, and shadow resources
            deployment_info = await self.deploy_pod(submission_id, user_id, docker_image)
            topic_name = str(submission_id)

            # Create the Redpanda topic synchronously after VM creation succeeds
            await self.create_redpanda_topic(topic_name)

            # Publish the updated Queue 2 payload
            await self.publish_to_queue2(
                submission_id,
                user_id,
                deployment_info.get("pod_name"),
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
        logger.info("Starting VM Creator worker...")
        
        subscription_path = self.subscriber.subscription_path(
            self.project_id,
            settings.queue1_subscription_name
        )
        
        streaming_pull_future = self.subscriber.subscribe(
            subscription_path,
            callback=lambda msg: asyncio.create_task(self.message_callback(msg))
        )
        
        try:
            streaming_pull_future.result(timeout=None)
        except Exception as e:
            logger.error(f"Listener error: {e}")
            streaming_pull_future.cancel()


async def run_worker() -> None:
    """Run the VM creator worker."""
    worker = VMCreatorWorker()
    await worker.start_listener()
