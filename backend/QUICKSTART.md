# Quick Start Guide - Tradeforces Microservices

## 🚀 Quick Setup

### 1. Install Dependencies

```bash
# Main service
cd backend/microservices/main
pip install -r requirements.txt

# VM Creator service
cd ../vm_creator
pip install -r requirements.txt

# Tests (optional)
cd ../../pytests
pip install -r requirements.txt
```

### 2. Configure Environment

```bash
# Main service
cd backend/microservices/main
cp .env.example .env
# Edit .env with your actual GCP credentials

# VM Creator service
cd ../vm_creator
cp .env.example .env
# Edit .env with your actual GCP credentials
```

**Required GCP Setup:**
- Cloud SQL PostgreSQL instance
- Pub/Sub topics: `submissions-queue`, `results-queue`
- Pub/Sub subscriptions: `submissions-queue-sub`
- Artifact Registry repository
- Cloud Build API enabled
- Service account with Storage, Pub/Sub, Build permissions
- K3s cluster with Kata runtime

### 3. Run Services Locally

```bash
# Terminal 1 - Main Service
cd backend/microservices/main
python -m uvicorn main:app --reload --port 8000

# Terminal 2 - VM Creator Service
cd backend/microservices/vm_creator
python -m uvicorn main:app --reload --port 8001
```

### 4. Test API

```bash
# Health check
curl http://localhost:8000/health

# Login
curl -X POST http://localhost:8000/login/credentials \
  -H "Content-Type: application/json" \
  -d '{"username":"testuser","password":"testpass"}'

# Get presigned URL (replace TOKEN)
curl -H "Authorization: Bearer TOKEN" \
  http://localhost:8000/submit/1
```

### 5. Run Tests

```bash
cd backend/pytests
pytest -v
```

## 📋 Service Ports

- **Main Service**: `http://localhost:8000`
- **VM Creator Service**: `http://localhost:8001`

## 🔑 Key Endpoints

### Main Service
| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/login/credentials` | User authentication |
| GET | `/submit/{submission_id}` | Get presigned upload URL |
| POST | `/upload_complete/{submission_id}` | Mark upload complete |
| GET | `/health` | Health check |

### VM Creator Service
| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/health` | Health check |
| GET | `/metrics` | Service metrics |

## 🗄️ Database Setup

```bash
# Connect to Cloud SQL
cloud_sql_proxy -instances=PROJECT:REGION:INSTANCE=tcp:5432

# Create database
psql -h localhost -U postgres
CREATE DATABASE tradeforces;
```

Tables will be created by SQLAlchemy when services start (or manually):
```sql
CREATE TABLE "user" (
  id SERIAL PRIMARY KEY,
  username VARCHAR(50) UNIQUE,
  name VARCHAR(100),
  email VARCHAR(100) UNIQUE,
  password_hash VARCHAR(200)
);

CREATE TABLE submission (
  id SERIAL PRIMARY KEY,
  user_id INT REFERENCES "user"(id),
  gcs_url VARCHAR(200),
  status VARCHAR(20),
  correctness_score NUMERIC(8,6),
  tps_score NUMERIC(8,6),
  final_score NUMERIC(8,6),
  error_message VARCHAR(500)
);
```

## 🐳 Docker Build

```bash
# Main service
docker build -f Dockerfile.main -t gcr.io/PROJECT/tradeforces-main:latest .
docker push gcr.io/PROJECT/tradeforces-main:latest

# VM Creator service
docker build -f Dockerfile.vmcreator -t gcr.io/PROJECT/tradeforces-vmcreator:latest .
docker push gcr.io/PROJECT/tradeforces-vmcreator:latest
```

## ☸️ Kubernetes Deployment

```bash
# Apply manifests
kubectl apply -f k3s/deployment-main.yaml
kubectl apply -f k3s/deployment-vmcreator.yaml

# Check status
kubectl get pods
kubectl logs pod/tradeforces-main-*
kubectl logs pod/tradeforces-vmcreator-*
```

## 🔍 Debugging

### Check logs
```bash
# Tail main service
tail -f /tmp/main_service.log

# Tail vm_creator service
tail -f /tmp/vmcreator_service.log
```

### Check Pub/Sub messages
```bash
# List topics
gcloud pubsub topics list

# Pull messages
gcloud pubsub subscriptions pull submissions-queue-sub --limit=1
```

### Check Cloud Build
```bash
gcloud builds list
gcloud builds log BUILD_ID
```

### Check Kubernetes
```bash
# Pod status
kubectl describe pod POD_NAME

# Pod logs
kubectl logs -f POD_NAME

# Port forward
kubectl port-forward pod/POD_NAME 8080:8000
```

## ⚙️ Configuration Reference

See `.env.example` files in each service directory for all available settings.

**Common settings:**
- `PROJECT_ID`: GCP project ID
- `QUEUE1_NAME`: Submission queue (main → vmcreator)
- `QUEUE2_NAME`: Results queue (vmcreator → frontend)
- `MAX_PENDING_MICROVMS`: Max VMs being created simultaneously
- `RESET_TTL`: Pub/Sub lease extension interval (seconds)

## 📚 Documentation

- **Full Documentation**: See `backend/README.md`
- **API Details**: Check route files in each service
- **Database Schema**: See `shared_core/DB_models.py`
- **Message Formats**: See `shared_core/schema.json`

## 🆘 Troubleshooting

| Issue | Solution |
|-------|----------|
| "No module named 'shared_core'" | Add `sys.path.insert(0, '..')` in service files |
| "403 Permission denied" GCS | Check service account IAM roles |
| "Kubernetes connection failed" | Run `gcloud container clusters get-credentials` |
| "Pub/Sub messages not consumed" | Verify subscription exists and has messages |
| "Cloud Build fails" | Check `Dockerfile` in submission ZIP and registry permissions |

## 🎯 Workflow Summary

```
Client
  ↓
1. Login → Get JWT Token
  ↓
2. GET /submit → Get Presigned URL
  ↓
3. Upload ZIP to GCS
  ↓
4. POST /upload_complete → Trigger Pipeline
  ↓
Main Service publishes to Queue 1
  ↓
VM Creator Service consumes message
  ↓
- Cloud Build builds image
- K3s Pod deployed
- Pod IP extracted
  ↓
Result published to Queue 2
  ↓
DB updated, Frontend notified
```

## 📞 Support

Check logs in services or submit issues to the project repository.
