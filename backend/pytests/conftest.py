"""Pytest configuration and fixtures."""

import pytest
import asyncio
import os
import sys
from fastapi.testclient import TestClient                                    # NEW: needed by db_client
from sqlalchemy.ext.asyncio import AsyncSession, create_async_engine, async_sessionmaker
# REMOVED: `from sqlalchemy import event` — was imported but never used

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from shared_core.DB_models import Base, User, Submission
from shared_core.auth import hash_password                                  # NEW: fixes sample_user + used by db_client_with_user


# ── sync helper ───────────────────────────────────────────────────────────────
# db_client is a *sync* fixture (so TestClient works without async complications)
# but SQLAlchemy setup is async. _run() lets us call async setup code from a
# sync fixture without touching or conflicting with pytest-asyncio's event loop.
def _run(coro):
    """Run an async coroutine in a fresh, isolated event loop."""
    loop = asyncio.new_event_loop()
    try:
        return loop.run_until_complete(coro)
    finally:
        loop.close()


# ── session-scoped event loop ─────────────────────────────────────────────────
# Required so that async fixtures (test_db, sample_user, sample_submission)
# all share one loop for the entire test session instead of creating a new one
# per test — which causes "event loop is closed" errors with asyncpg/aiosqlite.
@pytest.fixture(scope="session")
def event_loop():
    """Session-scoped event loop for all async fixtures."""
    loop = asyncio.new_event_loop()
    yield loop
    loop.close()


# ── test_db ───────────────────────────────────────────────────────────────────
# Raw async session factory — used directly by the async fixtures below and by
# vm_creator tests that need their own DB access without going through the app.
# Each test gets a FRESH in-memory SQLite database (no shared state between tests).
@pytest.fixture
async def test_db():
    """In-memory SQLite DB; yields the session factory, tears down after the test."""
    engine = create_async_engine("sqlite+aiosqlite:///:memory:")

    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)

    session_factory = async_sessionmaker(
        engine, class_=AsyncSession, expire_on_commit=False
    )

    yield session_factory

    await engine.dispose()


# ── sample_user ───────────────────────────────────────────────────────────────
# FIXED: was `password_hash="hashed_password"` (a plain string).
# Now that auth.py uses bcrypt, verify_password_hash("hashed_password", "hashed_password")
# raises because it's not a valid bcrypt digest.  Use hash_password() so any
# test that calls /login/credentials with ("testuser", "testpass") actually works.
@pytest.fixture
async def sample_user(test_db):
    """Seeded user with a real bcrypt-hashed password."""
    async with test_db() as session:
        user = User(
            username="testuser",
            name="Test User",
            email="test@example.com",
            password_hash=hash_password("testpass"),    # CHANGED from "hashed_password"
        )
        session.add(user)
        await session.commit()
        await session.refresh(user)
        return user


# ── sample_submission ─────────────────────────────────────────────────────────
# Unchanged — still depends on sample_user for the foreign key.
@pytest.fixture
async def sample_submission(test_db, sample_user):
    """Seeded submission owned by sample_user."""
    async with test_db() as session:
        submission = Submission(
            user_id=sample_user.id,
            gcs_url="gs://bucket/submissions/1/1.zip",
            status="uploading",
        )
        session.add(submission)
        await session.commit()
        await session.refresh(submission)
        return submission


# ── db_client ─────────────────────────────────────────────────────────────────
# NEW: this is the fixture that makes TestRegisterEndpoints (and the reworked
# TestAuthEndpoints / TestUploadEndpoints) actually test real logic instead of
# always hitting PostgreSQL and getting 500s.
#
# What it does:
#   1. Creates a fresh in-memory SQLite DB and runs all migrations.
#   2. Overrides the app's `get_db_session` dependency so every request uses
#      that SQLite DB instead of the real PostgreSQL connection string.
#   3. Yields a TestClient — the test runs here.
#   4. Clears the override and disposes the engine so the next test starts clean.
#
# Why sync instead of async: TestClient is synchronous by design (it uses
# threading internally).  Mixing it with `async def` fixtures causes subtle
# event-loop conflicts.  _run() handles the async setup/teardown from sync land.
@pytest.fixture
def db_client():
    """
    Sync TestClient for the main service wired to an in-memory SQLite DB.
    No PostgreSQL or GCP credentials required. Fully isolated per test.
    """
    # Deferred imports: conftest loads before sys.path insertions in the test
    # files, so import here (inside the fixture) to guarantee the path is set.
    from microservices.main.main import app
    from microservices.main.routes import get_db_session

    engine = create_async_engine("sqlite+aiosqlite:///:memory:")

    async def _setup():
        async with engine.begin() as conn:
            await conn.run_sync(Base.metadata.create_all)

    _run(_setup())

    session_factory = async_sessionmaker(
        engine, class_=AsyncSession, expire_on_commit=False
    )

    # The override must be an async generator matching get_db_session's signature
    async def override_db():
        async with session_factory() as session:
            yield session

    app.dependency_overrides[get_db_session] = override_db

    with TestClient(app) as client:
        yield client                        # ← test runs here

    # Cleanup: remove override so other fixtures/tests see the unmodified app
    app.dependency_overrides.clear()
    _run(engine.dispose())


# ── db_client_with_user ───────────────────────────────────────────────────────
# NEW: convenience fixture for tests that need a pre-existing user without
# repeating the registration call in every test body.
# Builds on db_client (gets the same isolated DB) and seeds via /register so
# the password is always bcrypt-hashed correctly — no manual hash_password() call.
#
# Usage:
#   def test_something(self, db_client_with_user):
#       client, creds = db_client_with_user
#       resp = client.post("/login/credentials",
#                          json={"username": creds["username"],
#                                "password": creds["password"]})
@pytest.fixture
def db_client_with_user(db_client):
    """
    db_client pre-seeded with one user created through /register.
    Returns (client, user_dict) so tests can log in with known credentials.
    """
    user_payload = {
        "username": "seededuser",
        "name":     "Seeded User",
        "email":    "seeded@example.com",
        "password": "seededpass123",
    }
    resp = db_client.post("/register", json=user_payload)
    assert resp.status_code == 201, (
        f"Seed user creation failed ({resp.status_code}): {resp.text}\n"
        f"Check that /register is implemented and passlib[bcrypt] is installed."
    )
    return db_client, user_payload