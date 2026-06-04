"""Worker service for VM creation."""

import asyncio
import json
import logging
from typing import Optional, Dict, Any
import google.auth
from google.cloud import pubsub_v1
from google.cloud import cloudbuild_v1
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
        """Load Kata microVM Pod template."""
        template_path = os.path.join(os.path.dirname(__file__), "microvm_pod.tmpl")
        try:
            with open(template_path, "r") as f:
                return f.read()
        except FileNotFoundError:
            logger.warning(f"Pod template not found at {template_path}")
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
    ) -> str:
        """Deploy Kata microVM Pod and return Pod IP."""
        logger.info(f"Deploying Pod for submission {submission_id}")
        
        # Render Pod template
        template = Template(self.pod_template)
        pod_yaml = template.render(
            submission_id=submission_id,
            user_id=user_id,
            docker_image=docker_image,
            namespace=settings.k3s_namespace
        )
        
        # Parse YAML and create Pod
        import yaml
        pod_dict = yaml.safe_load(pod_yaml)
        
        pod = self.k8s_client.create_namespaced_pod(
            namespace=settings.k3s_namespace,
            body=pod_dict
        )
        logger.info(f"Pod created: {pod.metadata.name}")
        
        # Watch Pod until Running
        pod_ip = await self._wait_for_pod_running(submission_id)
        
        return pod_ip
    
    async def _wait_for_pod_running(self, submission_id: int) -> str:
        """Wait for Pod to reach Running phase and extract IP."""
        logger.info(f"Waiting for Pod {submission_id} to reach Running phase")
        
        pod_name = str(submission_id)
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
                    logger.info(f"Pod {submission_id} is running with IP: {pod_ip}")
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
    
    async def publish_to_queue2(
        self,
        submission_id: int,
        user_id: int,
        docker_image: str,
        target_ip: str
    ) -> None:
        """Publish result to Queue 2."""
        logger.info(f"Publishing result for submission {submission_id} to Queue 2")
        
        message_data = {
            "submission_id": submission_id,
            "user_id": user_id,
            "docker_image": docker_image,
            "target_ip": target_ip
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
            
            # Deploy Pod
            target_ip = await self.deploy_pod(submission_id, user_id, docker_image)
            
            # Publish to Queue 2
            await self.publish_to_queue2(submission_id, user_id, docker_image, target_ip)
            
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
