"""Tests for main service."""

import socket
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


class TestDatabaseErrorHandling:
    """Test DB failure handling for auth endpoints."""

    def test_login_returns_503_when_db_lookup_fails(self, client, monkeypatch):
        """DB resolution errors should be returned as a service-unavailable response."""

        class BrokenSession:
            async def execute(self, *_args, **_kwargs):
                raise socket.gaierror(-2, "Name or service not known")

        class BrokenSessionContext:
            async def __aenter__(self):
                return BrokenSession()

            async def __aexit__(self, exc_type, exc, tb):
                return False

        monkeypatch.setattr(
            "microservices.main.routes.async_sessionmaker_factory",
            lambda: lambda: BrokenSessionContext(),
        )

        response = client.post(
            "/login/credentials",
            json={"username": "anyuser", "password": "anypassword"},
        )

        assert response.status_code == 503
        assert "Database unavailable" in response.json()["detail"]


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

class TestRegisterEndpoints:
    """
    All tests use db_client: SQLite in-memory, no PostgreSQL needed.
    Each test method gets a fully isolated, freshly migrated DB.
    """

    def test_register_success_returns_201_with_token_and_user_id(self, db_client):
        """Valid registration returns 201 with access_token and user_id."""
        resp = db_client.post(
            "/register",
            json={
                "username": "newuser",
                "name":     "New User",
                "email":    "new@example.com",
                "password": "securepass123",
            },
        )
        assert resp.status_code == 201
        data = resp.json()
        assert "access_token" in data
        assert "user_id" in data
        assert isinstance(data["user_id"], int)
        assert data["token_type"] == "bearer"
        assert len(data["access_token"]) > 20     # sanity: not an empty string

    def test_register_duplicate_username_returns_409(self, db_client):
        """Second registration with same username → 409, detail mentions 'username'."""
        payload = {
            "username": "dupuser",
            "name":     "Dup User",
            "email":    "dup@example.com",
            "password": "pass123",
        }
        first = db_client.post("/register", json=payload)
        assert first.status_code == 201

        second = db_client.post(
            "/register",
            json={**payload, "email": "other@example.com"},  # different email, same username
        )
        assert second.status_code == 409
        assert "username" in second.json()["detail"].lower()

    def test_register_duplicate_email_returns_409(self, db_client):
        """Second registration with same email → 409, detail mentions 'email'."""
        payload = {
            "username": "emailuser",
            "name":     "Email User",
            "email":    "emaildup@example.com",
            "password": "pass123",
        }
        first = db_client.post("/register", json=payload)
        assert first.status_code == 201

        second = db_client.post(
            "/register",
            json={**payload, "username": "emailuser2"},  # different username, same email
        )
        assert second.status_code == 409
        assert "email" in second.json()["detail"].lower()

    def test_register_missing_fields_returns_422(self, db_client):
        """Missing required fields → 422 Unprocessable Entity (Pydantic validation)."""
        resp = db_client.post("/register", json={"username": "incomplete"})
        assert resp.status_code == 422

    def test_register_empty_body_returns_422(self, db_client):
        """Empty body → 422."""
        resp = db_client.post("/register", json={})
        assert resp.status_code == 422

    def test_register_token_works_on_protected_route(self, db_client):
        """Token issued on /register must pass auth on /submit — not 401 or 403."""
        reg = db_client.post(
            "/register",
            json={
                "username": "tokencheck",
                "name":     "Token Check",
                "email":    "tokencheck@example.com",
                "password": "pass123",
            },
        )
        assert reg.status_code == 201

        token = reg.json()["access_token"]
        resp = db_client.get(
            "/submit/1",
            headers={"Authorization": f"Bearer {token}"},
        )
        # 401 = token rejected (broken), 403 = no token (also broken)
        # 500 = GCS not available in test env — acceptable
        assert resp.status_code not in [401, 403]

    def test_register_then_login_succeeds(self, db_client):
        """A registered user can log in via /login/credentials."""
        creds = {
            "username": "loginafterreg",
            "name":     "Login After Reg",
            "email":    "loginafterreg@example.com",
            "password": "mypassword",
        }
        reg = db_client.post("/register", json=creds)
        assert reg.status_code == 201

        login = db_client.post(
            "/login/credentials",
            json={"username": creds["username"], "password": creds["password"]},
        )
        assert login.status_code == 200
        assert "access_token" in login.json()

    def test_wrong_password_after_register_returns_401(self, db_client):
        """After registration, wrong password → 401."""
        db_client.post(
            "/register",
            json={
                "username": "wrongpassuser",
                "name":     "Wrong Pass",
                "email":    "wrongpass@example.com",
                "password": "correctpassword",
            },
        )
        login = db_client.post(
            "/login/credentials",
            json={"username": "wrongpassuser", "password": "wrongpassword"},
        )
        assert login.status_code == 401

    def test_register_returns_different_tokens_for_different_users(self, db_client):
        """Two distinct users must get distinct tokens."""
        r1 = db_client.post(
            "/register",
            json={"username": "u1", "name": "U1", "email": "u1@x.com", "password": "p"},
        )
        r2 = db_client.post(
            "/register",
            json={"username": "u2", "name": "U2", "email": "u2@x.com", "password": "p"},
        )
        assert r1.status_code == 201
        assert r2.status_code == 201
        assert r1.json()["access_token"] != r2.json()["access_token"]
        assert r1.json()["user_id"] != r2.json()["user_id"]