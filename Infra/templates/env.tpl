PROJECT_ID="project-cdd074dc-6291-4d7f-a2a"
GCS_BUCKET_NAME=${gcs_bucket_name}
ARTIFACT_REGISTRY_URL="us-central1-docker.pkg.dev/project-cdd074dc-6291-4d7f-a2a/tradeforces"
JWT_SECRET_KEY="2C9]WInX%Ms^@O4F[B9gg0s@|qKYTA4lGe|zI-llS@s"
SERVICE_ACCOUNT_EMAIL="artifact-registry-puller-898@project-cdd074dc-6291-4d7f-a2a.iam.gserviceaccount.com"

PRESIGNED_URL_EXPIRY_MINUTES=15
K3S_NAMESPACE=default

QUEUE1_NAME=${queue1_name}
QUEUE1_SUBSCRIPTION_NAME=${queue1_subscription_name}
RESET_TTL="5"
MAX_PENDING_MICROVMS="2"

QUEUE2_NAME=${queue2_name}
QUEUE2_SUBSCRIPTION_NAME=${queue2_subscription_name}

DB_HOST=${db_private_ip}
DB_NAME=${db_name}
DB_USER=${db_user}
DB_PASSWORD=${db_password}