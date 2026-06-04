"""Database configuration and session management."""

import os
from sqlalchemy.ext.asyncio import create_async_engine, AsyncSession, async_sessionmaker
from sqlalchemy.pool import NullPool

# Cloud SQL connection string: postgresql+asyncpg://user:password@host/dbname
def get_db_url() -> str:
    db_user = os.getenv("DB_USER", "postgres")
    db_password = os.getenv("DB_PASSWORD", "password")
    db_host = os.getenv("DB_HOST", "localhost")
    db_name = os.getenv("DB_NAME", "tradeforces")
    
    # For Cloud SQL via Unix sockets (on K3s):
    # postgresql+asyncpg://user:password@/dbname?unix_sock_dir=/cloudsql/project:region:instance
    
    return f"postgresql+asyncpg://{db_user}:{db_password}@{db_host}/{db_name}"


def create_async_db_engine():
    """Create async SQLAlchemy engine for Cloud SQL."""
    return create_async_engine(
        get_db_url(),
        echo=False,
        poolclass=NullPool,  # Better for serverless/K3s environments
    )


async def get_async_session():
    """Create async session factory."""
    engine = create_async_db_engine()
    async_session = async_sessionmaker(
        engine,
        class_=AsyncSession,
        expire_on_commit=False
    )
    return async_session
