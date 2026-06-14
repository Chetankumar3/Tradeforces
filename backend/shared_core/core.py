"""Database configuration and session management."""

import logging
import os
from urllib.parse import quote, quote_plus

from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker, create_async_engine
from sqlalchemy.pool import NullPool

from shared_core.DB_models import Base

logger = logging.getLogger(__name__)


def get_db_url() -> str:
    db_user = os.getenv("DB_USER", "postgres")
    db_password = os.getenv("DB_PASSWORD", "Ch92.8%%")
    db_host = os.getenv("DB_HOST", "34.73.220.233")
    db_name = os.getenv("DB_NAME", "testdb")
    db_port = os.getenv("DB_PORT", "5432")

    # Cloud SQL socket mode (for K3s / GKE sidecars) uses a Unix-socket path.
    if db_host.startswith("/cloudsql/"):
        return (
            f"postgresql+asyncpg://{quote_plus(db_user)}:{quote_plus(db_password)}"
            f"@/{db_name}?host={quote(db_host, safe='')}"
        )

    # Return clean URL without query string parameters
    return (
        f"postgresql+asyncpg://{quote_plus(db_user)}:{quote_plus(db_password)}"
        f"@{db_host}:{db_port}/{db_name}"
    )


_engine = None
_session_factory = None


def create_async_db_engine():
    """Create async SQLAlchemy engine for Cloud SQL."""
    global _engine
    if _engine is None:
        # Cast timeout to float to prevent asyncio TypeError
        timeout_val = float(os.getenv("DB_CONNECT_TIMEOUT", "10"))
        
        _engine = create_async_engine(
            get_db_url(),
            echo=False,
            poolclass=NullPool,
            pool_pre_ping=True,
            connect_args={"timeout": timeout_val}
        )
    return _engine


def async_sessionmaker_factory():
    """Create and cache the async session factory."""
    global _session_factory
    if _session_factory is None:
        _session_factory = async_sessionmaker(
            create_async_db_engine(),
            class_=AsyncSession,
            expire_on_commit=False,
        )
    return _session_factory


async def init_db() -> bool:
    """Ensure the database schema exists before serving requests."""
    try:
        engine = create_async_db_engine()
        async with engine.begin() as conn:
            await conn.run_sync(Base.metadata.create_all)
        logger.info("Database schema initialized.")
        return True
    except Exception as exc:
        logger.warning(
            "Database initialization skipped because the database is unavailable: %s",
            exc,
        )
        return False


async def get_async_session():
    """Create async session factory."""
    return async_sessionmaker_factory()