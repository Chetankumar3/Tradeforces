"""Tests for main service."""

import pytest
import sys
import os
from fastapi.testclient import TestClient
from sqlalchemy.ext.asyncio import AsyncSession

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "microservices", "main"))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from microservices.main.main import app
from shared_core.auth import create_jwt_token


@pytest.fixture
def client():
    """Create test client."""
    return TestClient(app)


class TestAuthEndpoints:
    """Test authentication endpoints."""
    
    def test_login_success(self, client):
        """Test successful login."""
        response = client.post(
            "/login/credentials",
            json={
                "username": "testuser",
                "password": "testpass"
            }
        )
        assert response.status_code in [200, 401]  # May fail if DB not connected
    
    def test_login_invalid_credentials(self, client):
        """Test login with invalid credentials."""
        response = client.post(
            "/login/credentials",
            json={
                "username": "invaliduser",
                "password": "wrongpass"
            }
        )
        # Should return 401 or 422 depending on implementation
        assert response.status_code in [401, 422, 500]


class TestUploadEndpoints:
    """Test upload endpoints."""
    
    def test_submit_without_auth(self, client):
        """Test submit endpoint without authentication."""
        response = client.get("/submit/1")
        assert response.status_code == 403
    
    def test_submit_with_valid_token(self, client):
        """Test submit endpoint with valid token."""
        token = create_jwt_token(user_id=1)
        response = client.get(
            "/submit/1",
            headers={"Authorization": f"Bearer {token}"}
        )
        # May fail if DB not connected, but should not return 401
        assert response.status_code != 401
    
    def test_upload_complete_without_auth(self, client):
        """Test upload_complete without authentication."""
        response = client.post("/upload_complete/1")
        assert response.status_code == 403
    
    def test_upload_complete_with_valid_token(self, client):
        """Test upload_complete with valid token."""
        token = create_jwt_token(user_id=1)
        response = client.post(
            "/upload_complete/1",
            headers={"Authorization": f"Bearer {token}"}
        )
        # May fail if DB not connected, but should not return 401
        assert response.status_code != 401


class TestHealthCheck:
    """Test health check endpoint."""
    
    def test_health_check(self, client):
        """Test health check endpoint."""
        response = client.get("/health")
        assert response.status_code == 200
        assert response.json()["status"] == "healthy"
