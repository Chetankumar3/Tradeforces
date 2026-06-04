"""Configuration for vm_creator service."""

import os
from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    """VM Creator service configuration."""
    
    # GCP
    project_id: str = os.getenv("PROJECT_ID", "your-project-id")
    artifact_registry_url: str = os.getenv("ARTIFACT_REGISTRY_URL", "")
    
    # Pub/Sub
    queue1_name: str = os.getenv("QUEUE1_NAME", "submissions-queue")
    queue1_subscription_name: str = os.getenv("QUEUE1_SUBSCRIPTION_NAME", "submissions-queue-sub")
    queue2_name: str = os.getenv("QUEUE2_NAME", "results-queue")
    
    # Worker settings
    reset_ttl: int = int(os.getenv("RESET_TTL", "30"))  # seconds
    max_pending_microvms: int = int(os.getenv("MAX_PENDING_MICROVMS", "10"))
    
    # Database
    db_user: str = os.getenv("DB_USER", "postgres")
    db_password: str = os.getenv("DB_PASSWORD", "password")
    db_host: str = os.getenv("DB_HOST", "localhost")
    db_name: str = os.getenv("DB_NAME", "tradeforces")
    
    # Kubernetes
    k3s_namespace: str = os.getenv("K3S_NAMESPACE", "default")
    
    # GCS
    gcs_bucket_name: str = os.getenv("GCS_BUCKET_NAME", "tradeforces-submissions")
    
    class Config:
        case_sensitive = False


settings = Settings()
