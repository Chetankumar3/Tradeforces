"""Pytest configuration and fixtures."""

import pytest
import asyncio
import os
from sqlalchemy.ext.asyncio import AsyncSession, create_async_engine, async_sessionmaker
from sqlalchemy import event

# Import models
import sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from shared_core.DB_models import Base, User, Submission


@pytest.fixture(scope="session")
def event_loop():
    """Create event loop for async tests."""
    loop = asyncio.new_event_loop()
    yield loop
    loop.close()


@pytest.fixture
async def test_db():
    """Create test database."""
    # Use SQLite for testing
    engine = create_async_engine("sqlite+aiosqlite:///:memory:")
    
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
    
    async_session = async_sessionmaker(engine, class_=AsyncSession, expire_on_commit=False)
    
    yield async_session
    
    await engine.dispose()


@pytest.fixture
async def sample_user(test_db):
    """Create sample user for testing."""
    async with test_db() as session:
        user = User(
            username="testuser",
            name="Test User",
            email="test@example.com",
            password_hash="hashed_password"
        )
        session.add(user)
        await session.commit()
        await session.refresh(user)
        return user


@pytest.fixture
async def sample_submission(test_db, sample_user):
    """Create sample submission for testing."""
    async with test_db() as session:
        submission = Submission(
            user_id=sample_user.id,
            gcs_url="gs://bucket/submissions/1/1.zip",
            status="uploading"
        )
        session.add(submission)
        await session.commit()
        await session.refresh(submission)
        return submission
