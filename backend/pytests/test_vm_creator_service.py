"""Tests for vm_creator service."""

import pytest
import sys
import os
import json
from fastapi.testclient import TestClient

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
        """Test Queue 2 message mapping."""
        mapper = SchemaMapper()
        
        internal_data = {
            "submission_id": 123,
            "user_id": 456,
            "docker_image": "gcr.io/image:latest",
            "target_ip": "10.0.0.1"
        }
        
        mapped = mapper.to_queue2(internal_data)
        
        assert "sub_id" in mapped
        assert "team_id" in mapped
        assert "compiled_image_tag" in mapped
        assert "microvm_internal_ip" in mapped
        assert mapped["sub_id"] == 123
        assert mapped["microvm_internal_ip"] == "10.0.0.1"
    
    def test_queue2_reverse_mapping(self):
        """Test reverse mapping from Queue 2."""
        mapper = SchemaMapper()
        
        queue2_message = {
            "sub_id": 123,
            "team_id": 456,
            "compiled_image_tag": "gcr.io/image:latest",
            "microvm_internal_ip": "10.0.0.1"
        }
        
        internal_data = mapper.from_queue2(queue2_message)
        
        assert "submission_id" in internal_data
        assert "user_id" in internal_data
        assert "docker_image" in internal_data
        assert "target_ip" in internal_data
        assert internal_data["submission_id"] == 123
        assert internal_data["target_ip"] == "10.0.0.1"


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
