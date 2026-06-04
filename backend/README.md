# Tradeforces Microservices Architecture

This project implements a high-frequency trading hackathon platform with two FastAPI microservices deployed on Google Cloud Platform (GCP).

## Project Structure

```
backend/
├── shared_core/                    # Shared utilities and models
│   ├── __init__.py
│   ├── DB_models.py               # SQLAlchemy ORM models (User, Submission)
│   ├── core.py                    # Database connection setup
│   ├── auth.py                    # JWT authentication utilities
│   ├── schema_mapper.py           # Pub/Sub message schema mapping
│   └── schema.json                # Message schema definitions
├── microservices/
│   ├── main/                       # Main API Gateway Service
│   │   ├── main.py                # FastAPI application
│   │   ├── routes.py              # API endpoints
│   │   ├── config.py              # Configuration settings
│   │   ├── requirements.txt       # Dependencies
│   │   └── .env.example           # Environment variables template
│   └── vm_creator/                 # VM Creation Worker Service
│       ├── main.py                # FastAPI application with worker
│       ├── worker.py              # Background worker for async processing
│       ├── config.py              # Configuration settings
│       ├── microvm_pod.tmpl       # Kubernetes Pod template (Jinja2)
│       ├── requirements.txt       # Dependencies
│       └── .env.example           # Environment variables template
└── pytests/                        # Integration tests
    ├── conftest.py                # Pytest configuration and fixtures
    ├── test_main_service.py       # Tests for main service
    ├── test_vm_creator_service.py # Tests for vm_creator service
    └── requirements.txt           # Test dependencies
```

## Services Overview

### 1. **Main Service** (`main/`)
The API Gateway service handles:
- **User Authentication**: JWT-based login
- **Presigned URL Generation**: AWS S3-style presigned URLs for GCS uploads
- **Upload Handling**: Tracks submission status and publishes to Pub/Sub Queue 1
- **Port**: 8000

**Key Endpoints:**
- `POST /login/credentials` - User authentication
- `GET /submit/{submission_id}` - Get presigned upload URL
- `POST /upload_complete/{submission_id}` - Mark upload complete and queue for VM creation
- `GET /health` - Health check

### 2. **VM Creator Service** (`vm_creator/`)
Async worker service handles:
- **Message Consumption**: Listens to Pub/Sub Queue 1
- **Cloud Build Integration**: Triggers image builds from submissions
- **Kubernetes Deployment**: Deploys Kata-isolated microVMs on K3s
- **Pod Monitoring**: Watches pods until Running and extracts Pod IP
- **Result Publishing**: Sends results to Pub/Sub Queue 2
- **Lease Management**: Extends Pub/Sub message leases during processing
- **Port**: 8001

**Features:**
- Pending VM counter with configurable max limit
- Async message processing with lease extension
- Dynamic Pod template rendering with Jinja2
- Automatic ACK only on successful completion

## Setup and Deployment

### Prerequisites

- **Python 3.10+**
- **GCP Project** with:
  - Cloud SQL (PostgreSQL) instance
  - Pub/Sub topics and subscriptions
  - Artifact Registry for container images
  - Cloud Build enabled
  - Service account with appropriate IAM roles
- **Kubernetes Cluster** (K3s or GKE) with:
  - Kata runtime configured
  - Network connectivity to Cloud SQL

### Environment Setup

1. **Copy `.env.example` to `.env`** in each service directory and configure:

```bash
# For main service
cd backend/microservices/main
cp .env.example .env
# Edit .env with your GCP credentials and database details

# For vm_creator service
cd ../vm_creator
cp .env.example .env
# Edit .env with your GCP credentials
```

2. **Install Dependencies**:

```bash
# Main service
cd backend/microservices/main
pip install -r requirements.txt

# VM Creator service
cd ../vm_creator
pip install -r requirements.txt

# For testing
cd ../../pytests
pip install -r requirements.txt
```

### Running Locally

```bash
# Terminal 1: Run main service
cd backend/microservices/main
python -m uvicorn main:app --reload --port 8000

# Terminal 2: Run vm_creator service
cd backend/microservices/vm_creator
python -m uvicorn main:app --reload --port 8001
```

### Docker Deployment

```dockerfile
# Dockerfile for main service
FROM python:3.11-slim
WORKDIR /app
COPY backend/microservices/main/requirements.txt .
RUN pip install -r requirements.txt
COPY backend .
CMD ["uvicorn", "microservices.main.main:app", "--host", "0.0.0.0", "--port", "8000"]
```

```dockerfile
# Dockerfile for vm_creator service
FROM python:3.11-slim
WORKDIR /app
COPY backend/microservices/vm_creator/requirements.txt .
RUN pip install -r requirements.txt
COPY backend .
CMD ["uvicorn", "microservices.vm_creator.main:app", "--host", "0.0.0.0", "--port", "8001"]
```

### Kubernetes Deployment

Deploy services using Helm or kubectl:

```bash
kubectl apply -f k3s/deployment-main.yaml
kubectl apply -f k3s/deployment-vm_creator.yaml
```

## API Workflow

### Submission Flow

```
1. User calls POST /login/credentials
   └─> Returns JWT token

2. User calls GET /submit/{submission_id}
   ├─> Creates DB record with status 'uploading'
   └─> Returns presigned GCS upload URL

3. User uploads submission ZIP to GCS (using presigned URL)

4. User calls POST /upload_complete/{submission_id}
   ├─> Updates DB status to 'queued_for_microVM_creation'
   ├─> Publishes to Queue 1 (with schema mapping)
   └─> Returns success

5. VM Creator service consumes Queue 1 message
   ├─> Triggers Cloud Build (builds docker image from submission)
   ├─> Waits for build completion
   ├─> Deploys Kata microVM pod with image
   ├─> Watches pod until Running
   ├─> Publishes to Queue 2 with pod IP and image
   ├─> Updates DB status to 'queued_for_loadgen_setup'
   └─> ACKs Queue 1 message
```

## Message Schema

Message transformation is handled by `schema_mapper.py` for compatibility:

### Queue 1 (Submissions to VM Creator)
```json
{
  "source_archive_url": "gs://bucket/submissions/user_id/submission_id.zip",
  "sub_id": 123,
  "team_id": 456
}
```

### Queue 2 (Results to Frontend)
```json
{
  "sub_id": 123,
  "team_id": 456,
  "compiled_image_tag": "gcr.io/project/submission-123:latest",
  "microvm_internal_ip": "10.0.0.5"
}
```

## Database Schema

### User Table
```sql
CREATE TABLE "user" (
  id INT PRIMARY KEY,
  username VARCHAR(50) UNIQUE NOT NULL,
  name VARCHAR(100) NOT NULL,
  email VARCHAR(100) UNIQUE NOT NULL,
  password_hash VARCHAR(200) NOT NULL
);
```

### Submission Table
```sql
CREATE TABLE submission (
  id INT PRIMARY KEY,
  user_id INT FOREIGN KEY REFERENCES "user"(id),
  gcs_url VARCHAR(200) NOT NULL,
  status VARCHAR(20) NOT NULL,
  correctness_score DECIMAL(8,6),
  tps_score DECIMAL(8,6),
  final_score DECIMAL(8,6),
  error_message VARCHAR(500)
);
```

## GCP Authentication

Both services use **Application Default Credentials (ADC)**:

```python
import google.auth
credentials, project_id = google.auth.default(
    scopes=["https://www.googleapis.com/auth/cloud-platform"]
)
```

This works seamlessly on:
- **Local dev**: Set via `gcloud auth application-default login`
- **GKE/K3s**: Via Workload Identity or service account
- **Cloud Run**: Automatic

## Key Implementation Details

### ✅ Architectural Compliance

- ✅ **GCP Auth**: Application Default Credentials (ADC) only
- ✅ **Presigned URLs**: V4 signed URLs from GCS
- ✅ **Kubernetes**: `load_incluster_config()` for K3s pods
- ✅ **Database**: SQLAlchemy 2.0 with async support
- ✅ **Pub/Sub**: Dynamic schema mapping from `schema.json`
- ✅ **No Docker SDK**: Cloud Build integration, no docker-py
- ✅ **Lease Extension**: Async tasks for message lease management
- ✅ **Error Handling**: Proper HTTPException and logging

### Performance Considerations

1. **Connection Pooling**: NullPool for serverless/K3s environments
2. **Async Processing**: All I/O operations are async/await
3. **Pending VM Limit**: Configurable to prevent resource exhaustion
4. **Message ACK Strategy**: ACK only after successful completion (ensures no message loss)

## Testing

Run tests with pytest:

```bash
cd backend/pytests
pytest -v
# or specific test file
pytest test_main_service.py -v
```

### Test Coverage

- Authentication endpoints
- Presigned URL generation
- Schema mapping
- JWT token validation
- Health checks

## Troubleshooting

### Issue: "No module named 'shared_core'"
**Solution**: Ensure `sys.path` includes parent directory or install as package

### Issue: "Permission denied" for GCS/Cloud Build
**Solution**: Check IAM roles for service account (Storage Admin, Cloud Build Editor)

### Issue: Kubernetes "failed to get reader" 
**Solution**: Verify `load_incluster_config()` or kubeconfig path

### Issue: Messages not being consumed
**Solution**: Check Pub/Sub subscription name and ensure it exists; verify topic publish permissions

## Contributing

When making changes:
1. Update schema.json if message formats change
2. Add tests for new endpoints
3. Update README with any architectural changes
4. Keep shared_core utilities generic for code reuse

## License

This project is part of the Tradeforces hackathon platform.
