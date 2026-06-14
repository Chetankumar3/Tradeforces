"""Tests for vm_creator service."""

import asyncio
import os
import sys
from types import SimpleNamespace

import pytest
import yaml
from fastapi.testclient import TestClient
from jinja2 import Template

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "microservices", "vm_creator"))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from microservices.vm_creator.main import app
from shared_core.schema_mapper import SchemaMapper


@pytest.fixture
def client():
    """Create test client."""
    return TestClient(app)


class TestHealthCheck:
    """Test health check endpoints."""
    
    def test_health_check(self, client):
        """Test health check endpoint."""
        response = client.get("/health")
        assert response.status_code == 200
        assert response.json()["status"] == "healthy"
    
    def test_metrics(self, client):
        """Test metrics endpoint."""
        response = client.get("/metrics")
        assert response.status_code == 200
        data = response.json()
        assert data["service"] == "vm_creator"


class TestSchemaMapper:
    """Test schema mapper for message transformation."""
    
    def test_queue1_mapping(self):
        """Test Queue 1 message mapping."""
        mapper = SchemaMapper()
        
        internal_data = {
            "gcs_url": "gs://bucket/file.zip",
            "submission_id": 123,
            "user_id": 456
        }
        
        mapped = mapper.to_queue1(internal_data)
        
        assert "source_archive_url" in mapped
        assert "sub_id" in mapped
        assert "team_id" in mapped
        assert mapped["sub_id"] == 123
        assert mapped["team_id"] == 456
    
    def test_queue1_reverse_mapping(self):
        """Test reverse mapping from Queue 1."""
        mapper = SchemaMapper()
        
        queue1_message = {
            "source_archive_url": "gs://bucket/file.zip",
            "sub_id": 123,
            "team_id": 456
        }
        
        internal_data = mapper.from_queue1(queue1_message)
        
        assert "gcs_url" in internal_data
        assert "submission_id" in internal_data
        assert "user_id" in internal_data
        assert internal_data["submission_id"] == 123
        assert internal_data["user_id"] == 456
    
    def test_queue2_mapping(self):
        """Test Queue 2 message mapping for the updated worker payload."""
        mapper = SchemaMapper()

        internal_data = {
            "submission_id": 123,
            "user_id": 456,
            "microvm_pod-name": "microvm-pod-123",
            "telemetry_pod-name": "telemetry-pod-123",
            "shadow_pod-name": "shadow-pod-123",
            "topic_name": "123"
        }

        mapped = mapper.to_queue2(internal_data)

        assert mapped["sub_id"] == 123
        assert mapped["team_id"] == 456
        assert mapped["microvm_pod-name"] == "microvm-pod-123"
        assert mapped["telemetry_pod-name"] == "telemetry-pod-123"
        assert mapped["shadow_pod-name"] == "shadow-pod-123"
        assert mapped["topic_name"] == "123"
    
    def test_queue2_reverse_mapping(self):
        """Test reverse mapping from Queue 2 using the new fields."""
        mapper = SchemaMapper()

        queue2_message = {
            "sub_id": 123,
            "team_id": 456,
            "microvm_pod-name": "microvm-pod-123",
            "telemetry_pod-name": "telemetry-pod-123",
            "shadow_pod-name": "shadow-pod-123",
            "topic_name": "123"
        }

        internal_data = mapper.from_queue2(queue2_message)

        assert internal_data["submission_id"] == 123
        assert internal_data["user_id"] == 456
        assert internal_data["microvm_pod-name"] == "microvm-pod-123"
        assert internal_data["telemetry_pod-name"] == "telemetry-pod-123"
        assert internal_data["shadow_pod-name"] == "shadow-pod-123"
        assert internal_data["topic_name"] == "123"


class TestVMCreatorTemplate:
    """Validate the manifest template used by the VM creator worker."""

    def test_template_renders_three_pod_resources(self):
        """All three resources in the manifest should be Pods, not Deployments."""
        template_path = os.path.join(
            os.path.dirname(__file__),
            "..",
            "microservices",
            "vm_creator",
            "create_3_pods.tmpl",
        )

        with open(template_path, "r", encoding="utf-8") as handle:
            rendered = Template(handle.read()).render(
                submission_id=42,
                user_id=7,
                docker_image="example/image:latest",
                namespace="tradeforces",
            )

        resources = list(yaml.safe_load_all(rendered))

        assert len(resources) == 3
        assert all(resource["kind"] == "Pod" for resource in resources)
        assert [resource["metadata"]["name"] for resource in resources] == [
            "microvm-42",
            "telemetry-42",
            "shadow-42",
        ]


class TestVMCreatorWorker:
    """Regression tests for worker helpers."""

    def test_telemetry_template_uses_runtime_pod_ip_with_fixed_ports(self):
        """Telemetry env values should keep the fixed ports while using the runtime Pod IP."""
        template_path = os.path.join(
            os.path.dirname(__file__),
            "..",
            "microservices",
            "vm_creator",
            "create_3_pods.tmpl",
        )

        with open(template_path, "r", encoding="utf-8") as handle:
            rendered = Template(handle.read()).render(
                submission_id=42,
                user_id=7,
                docker_image="example/image:latest",
                namespace="tradeforces",
                contestant_ingress_addr="10.0.0.13",
                contestant_egress_addr="10.0.0.13",
                shadow_egress_addr="shadow-42.tradeforces",
                redpanda_brokers="broker:9092",
                redpanda_orders_topic="orders",
                redpanda_results_topic="results",
                redpanda_sasl_username="user",
                redpanda_sasl_password="pass",
                redpanda_sasl_mechanism="SCRAM-SHA-256",
            )

        resources = list(yaml.safe_load_all(rendered))
        telemetry = next(resource for resource in resources if resource["metadata"]["name"] == "telemetry-42")

        env_map = {item["name"]: item["value"] for item in telemetry["spec"]["containers"][0]["env"]}

        assert env_map["CONTESTANT_INGRESS_ADDR"] == "10.0.0.13:9100"
        assert env_map["CONTESTANT_EGRESS_ADDR"] == "10.0.0.13:9101"
        assert env_map["SHADOW_EGRESS_ADDR"] == "shadow-42.tradeforces:9100"

    def test_create_redpanda_topic_uses_kafka_admin_client(self, monkeypatch):
        """The worker should create Redpanda topics through the Kafka admin client."""
        worker_module = __import__(
            "microservices.vm_creator.worker",
            fromlist=["VMCreatorWorker"],
        )

        worker = object.__new__(worker_module.VMCreatorWorker)
        captured = {}

        class FakeNewTopic:
            def __init__(self, *args, **kwargs):
                self.name = args[0] if args else kwargs.get("name")
                captured["topic_args"] = args
                captured["topic_kwargs"] = {"name": self.name, **kwargs}

        class FakeKafkaAdminClient:
            def __init__(self, *args, **kwargs):
                captured["admin_conf"] = args[0] if args else None
                captured["admin_kwargs"] = kwargs

            def create_topics(self, new_topics):
                captured["topics"] = new_topics
                return {"submission-42": SimpleNamespace(result=lambda: None)}

            def close(self):
                captured["closed"] = True

        monkeypatch.setitem(sys.modules, "confluent_kafka", SimpleNamespace(KafkaException=RuntimeError))
        monkeypatch.setitem(sys.modules, "confluent_kafka.admin", SimpleNamespace(AdminClient=FakeKafkaAdminClient, NewTopic=FakeNewTopic))

        monkeypatch.setattr(worker_module.settings, "redpanda_bootstrap_servers", "broker-1:9092,broker-2:9092", raising=False)
        monkeypatch.setattr(worker_module.settings, "redpanda_sasl_username", "rp_user", raising=False)
        monkeypatch.setattr(worker_module.settings, "redpanda_sasl_password", "rp_pass", raising=False)
        monkeypatch.setattr(worker_module.settings, "redpanda_sasl_mechanism", "SCRAM-SHA-256", raising=False)

        asyncio.run(worker.create_redpanda_topic("submission-42"))

        assert captured["admin_conf"]["bootstrap.servers"] == "broker-1:9092,broker-2:9092"
        assert captured["admin_conf"]["security.protocol"] == "SASL_SSL"
        assert captured["admin_conf"]["sasl.mechanisms"] == "SCRAM-SHA-256"
        assert captured["admin_conf"]["sasl.username"] == "rp_user"
        assert captured["admin_conf"]["sasl.password"] == "rp_pass"
        assert len(captured["topics"]) == 1
        assert captured["topics"][0].name == "submission-42"
        assert captured["topic_kwargs"]["name"] == "submission-42"
        assert captured["topic_kwargs"]["num_partitions"] == 1
        assert captured["topic_kwargs"]["replication_factor"] == 3
        assert captured["closed"] is True

    def test_build_render_context_populates_required_env_values(self):
        """The manifest render context should include the required topic and port-address values."""
        worker_module = __import__("microservices.vm_creator.worker", fromlist=["VMCreatorWorker"])
        worker = object.__new__(worker_module.VMCreatorWorker)

        context = worker._build_render_context(
            submission_id=42,
            user_id=7,
            docker_image="example/image:latest",
            namespace="tradeforces",
            pod_ip="10.0.0.13",
            topic_name="42",
        )

        assert context["submission_id"] == 42
        assert context["user_id"] == 7
        assert context["docker_image"] == "example/image:latest"
        assert context["namespace"] == "tradeforces"
        assert context["contestant_ingress_addr"] == "10.0.0.13"
        assert context["contestant_egress_addr"] == "10.0.0.13"
        assert context["shadow_egress_addr"] == "shadow-42.tradeforces"
        assert context["redpanda_brokers"] == worker_module.settings.redpanda_bootstrap_servers
        assert context["redpanda_orders_topic"] == "42"
        assert context["redpanda_results_topic"] == "42"

    def test_wait_for_pod_running_waits_for_ip(self, monkeypatch):
        """The worker should not return until the Pod has an assigned IP."""
        worker = object.__new__(__import__("microservices.vm_creator.worker", fromlist=["VMCreatorWorker"]).VMCreatorWorker)

        responses = iter([
            SimpleNamespace(status=SimpleNamespace(phase="Running", pod_ip=None), metadata=SimpleNamespace(name="microvm-42")),
            SimpleNamespace(status=SimpleNamespace(phase="Running", pod_ip="10.0.0.5"), metadata=SimpleNamespace(name="microvm-42")),
        ])

        worker.k8s_client = SimpleNamespace(
            read_namespaced_pod=lambda name, namespace: next(responses)
        )

        async def fake_sleep(_seconds):
            return None

        monkeypatch.setattr(asyncio, "sleep", fake_sleep)

        pod_ip = asyncio.run(worker._wait_for_pod_running("microvm-42"))

        assert pod_ip == "10.0.0.5"


class TestJWTAuth:
    """Test JWT authentication utilities."""
    
    def test_jwt_token_creation(self):
        """Test JWT token creation."""
        from shared_core.auth import create_jwt_token, decode_jwt_token
        
        token = create_jwt_token(user_id=123)
        assert isinstance(token, str)
        assert len(token) > 0
        
        decoded_user_id = decode_jwt_token(token)
        assert decoded_user_id == 123
    
    def test_jwt_invalid_token(self):
        """Test JWT with invalid token."""
        from shared_core.auth import decode_jwt_token
        
        decoded_user_id = decode_jwt_token("invalid.token.here")
        assert decoded_user_id is None
