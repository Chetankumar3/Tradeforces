"""Configuration for main service."""

import os
from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    """Main service configuration."""
    
    # GCP
    project_id: str = os.getenv("PROJECT_ID", "your-project-id")
    gcs_bucket_name: str = os.getenv("GCS_BUCKET_NAME", "your-bucket")
    artifact_registry_url: str = os.getenv("ARTIFACT_REGISTRY_URL", "")
    service_account_email: str = os.getenv("SERVICE_ACCOUNT_EMAIL", "")
    
    # Pub/Sub
    queue1_name: str = os.getenv("QUEUE1_NAME", "submissions-queue")
    queue1_subscription: str = os.getenv("QUEUE1_SUBSCRIPTION_NAME", "submissions-queue-sub")
    queue2_name: str = os.getenv("QUEUE2_NAME", "results-queue")
    
    # Database
    db_user: str = os.getenv("DB_USER", "postgres")
    db_password: str = os.getenv("DB_PASSWORD", "password")
    db_host: str = os.getenv("DB_HOST", "localhost")
    db_name: str = os.getenv("DB_NAME", "tradeforces")
    
    # JWT
    jwt_secret_key: str = os.getenv("JWT_SECRET_KEY", "your-secret-key-change-in-production")
    
    # Settings
    presigned_url_expiry_minutes: int = 15
    
    class Config:
        case_sensitive = False


settings = Settings()
