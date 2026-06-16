# Tradeforces — High-Frequency Trading Hackathon Platform

**A cloud-native, high-throughput evaluation system for algorithmic trading submissions with microsecond-precision latency measurement and correctness validation.**

---

## 📋 Table of Contents
- [Project Overview](#project-overview)
- [Tech Stack](#tech-stack)
- [Architecture](#architecture)
- [Getting Started](#getting-started)
- [Project Structure](#project-structure)
- [Deployment](#deployment)
- [Resources](#resources)

---

## 🎯 Project Overview

**Tradeforces** is a complete platform for evaluating high-frequency trading algorithms in a sandbox environment. Teams submit trading engine code that:

1. Gets uploaded to **Google Cloud Storage (GCS)** via presigned URLs
2. Is automatically built into isolated microVM container images via **Cloud Build**
3. Deploys to a **Kubernetes (k3s)** cluster as containerized microVMs (Kata isolation)
4. Receives 100,000+ orders/second from a **Redpanda** message broker via persistent TCP connections
5. Is measured for latency (nanosecond precision) and correctness against a reference shadow engine
6. Receives a final composite score published back to **Redpanda**

The platform is designed for **extreme throughput** (100K+ RPS) with **zero-allocation hot paths**, leveraging channel-based concurrency in Go and async/await patterns in Python.

---

## 🛠️ Tech Stack

### **Cloud Infrastructure**
- **Google Cloud Pub/Sub**: Message queuing for async handoffs between microservices
- **Google Cloud Build**: Container image building from submission source
- **Google Cloud Storage (GCS)**: Submission storage with V4 presigned URLs
- **Google Cloud Artifact Registry**: Docker image registry for contestant engines
- **Google Cloud SQL**: PostgreSQL database (async SQLAlchemy 2.0)

### **Kubernetes & Containerization**
- **Kubernetes (k3s)**: Lightweight orchestration cluster
- **Kata Containers**: Hardware-isolated microVMs for contestant engines
- **Docker**: Containerization for all microservices

### **Message Streaming**
- **Redpanda**: Kafka-compatible event broker for order ingestion and score publishing
- **franz-go**: High-performance Redpanda client in Go

### **Backend Services**
- **Python 3.10+**
  - FastAPI: Main API gateway & async microservices
  - SQLAlchemy 2.0: Async ORM for database operations
  - Pydantic v2: Data validation & serialization
  - JWT: Authentication (HS256)
  
- **Go 1.20+**
  - Zero-allocation designs for hot paths
  - Channel-based concurrency (no mutexes)
  - HDR Histogram: Latency percentile tracking
  - bufio & syscall: Low-level TCP for sub-microsecond timing

### **Frontend**
- **React 18 + TypeScript**: Single-page application (SPA)
- **Vite**: Next-gen build tool
- **TanStack Query**: Server state management
- **Tailwind CSS**: Utility-first styling
- **React Router v6**: Client-side navigation

---

## 🏗️ Architecture

### **Three Tiers**

#### **1. API Gateway (`main` service — Python FastAPI)**
- JWT-based authentication
- Endpoint: `POST /login/credentials` — User login
- Endpoint: `GET /submit/{submission_id}` — Get presigned GCS upload URL
- Endpoint: `POST /upload_complete/{submission_id}` — Trigger build pipeline
- Publishes submission metadata to **Pub/Sub Queue 1**
- Updates PostgreSQL with status tracking

#### **2. Async Worker (`vm_creator` service — Python FastAPI)**
- Consumes from **Pub/Sub Queue 1**
- Triggers **Cloud Build** to build the submission image
- Deploys Kata-isolated Pod to **k3s cluster**
- Extracts Pod IP when running
- Publishes result to **Pub/Sub Queue 2**
- Manages backpressure: `MAX_PENDING_MICROVMS` to prevent resource exhaustion
- ACKs Pub/Sub messages only after successful handoff

#### **3. Evaluation Harness (`telemetry` service — Go)**
- **Go1A**: Ingress Consumer — Pulls orders from Redpanda topic
- **Go1B**: Ingress TCP Writer — Streams orders to contestant engine over persistent TCP
- **Go2**: Egress TCP Reader — Reads execution reports from contestant engine
- **Go3**: Correctness Validator — Compares contestant output vs. shadow engine output
- **Go4**: Latency Manager — Records timestamps, computes HDR histogram percentiles
- **Go6**: Scorer — Calculates composite score (latency + correctness) every interval
- **Go7**: Shadow Engine Reader — Reads from reference engine for correctness checks
- Publishes final scores to Redpanda results topic

### **Data Flow**

```
Team Submission (Frontend)
    ↓
[API Gateway] → GCS Upload (presigned URL)
    ↓
[API Gateway] → Pub/Sub Queue 1
    ↓
[VM Creator] → Cloud Build → Image in Artifact Registry
    ↓
[VM Creator] → Deploy Pod (k3s)
    ↓
[Telemetry] ← Redpanda (Orders) → TCP (contestant engine)
    ↓
[Telemetry] ← TCP (contestant exec reports + shadow engine reports)
    ↓
[Telemetry] → HDR Histogram (latency) + Correctness Validation
    ↓
[Telemetry] → Redpanda Results Topic (Score)
```

---

## 🚀 Getting Started

### **Prerequisites**
- Python 3.10+
- Go 1.20+
- Docker & Docker Compose
- kubectl configured for k3s cluster
- GCP Project with:
  - Service Account with Cloud Build, GCS, Pub/Sub, Artifact Registry permissions
  - PostgreSQL instance in Cloud SQL
  - Redpanda cluster (or local instance for development)

### **Local Setup**

#### **1. Clone & Install Dependencies**
```bash
cd backend
pip install -r requirements.txt
pip install -r microservices/main/requirements.txt
pip install -r microservices/vm_creator/requirements.txt
pip install -r microservices/extra_services/requirements.txt
```

#### **2. Frontend Setup**
```bash
cd frontend
npm install
npm run dev
```

#### **3. Environment Variables**

Create `.env` files for each microservice:

**`backend/microservices/main/.env`**
```env
PROJECT_ID=your-gcp-project
GCS_BUCKET_NAME=submissions-bucket
QUEUE1_NAME=submissions-queue
JWT_SECRET_KEY=your-secret
SERVICE_ACCOUNT_EMAIL=sa@project.iam.gserviceaccount.com
DB_USER=postgres
DB_PASSWORD=password
DB_HOST=cloudsql-proxy
DB_NAME=tradeforces
```

**`backend/microservices/vm_creator/.env`**
```env
ARTIFACT_REGISTRY_URL=us-central1-docker.pkg.dev/project/repo
QUEUE1_NAME=submissions-queue
QUEUE1_SUBSCRIPTION_NAME=submissions-sub
QUEUE2_NAME=results-queue
MAX_PENDING_MICROVMS=10
RESET_TTL=30
DB_USER=postgres
DB_PASSWORD=password
DB_HOST=cloudsql-proxy
DB_NAME=tradeforces
```

**`backend/microservices/telemetry/.env`**
```env
REDPANDA_BROKER=localhost:9092
INGRESS_TOPIC=orders
RESULTS_TOPIC=scores
SUBMISSION_ID=contest-001
SCORING_INTERVAL_MS=1000
```

#### **4. Run Services**
```bash
# Terminal 1: Main API Gateway
cd backend/microservices/main && uvicorn main:app --reload --port 8000

# Terminal 2: VM Creator Worker
cd backend/microservices/vm_creator && uvicorn main:app --reload --port 8001

# Terminal 3: Go Telemetry Harness
cd backend/microservices/telemetry && go run main.go

# Terminal 4: Frontend
cd frontend && npm run dev
```

#### **5. Run Tests**
```bash
# Python tests
python -m pytest backend/pytests/ -v

# C++ Engine Tests (Contestant & Shadow)
cd backend/microservices/contestant_engine
cmake -B build-tests -DBUILD_TESTS=ON && cmake --build build-tests && ctest --test-dir build-tests

cd ../shadow_engine
cmake -B build-tests -DBUILD_TESTS=ON && cmake --build build-tests && ctest --test-dir build-tests
```

---

## 📁 Project Structure

```
Tradeforces/
├── backend/
│   ├── microservices/
│   │   ├── main/                     # FastAPI gateway (auth, uploads)
│   │   ├── vm_creator/               # Async worker (Cloud Build, k3s deployment)
│   │   ├── contestant_engine/        # C++ trading engine (compiled to microVM image)
│   │   ├── shadow_engine/            # C++ reference engine (correctness validation)
│   │   ├── telemetry/                # Go harness (latency + correctness measurement)
│   │   ├── extra_services/           # DashPusher & other side services
│   │   └── loadgen/                  # Load generation controller & runner
│   ├── pytests/                      # Integration tests
│   ├── shared_core/                  # SQLAlchemy models, auth, schema mapping
│   └── docker-build.sh               # Multi-container build script
│
├── frontend/                         # React + TypeScript SPA
│   ├── src/
│   │   ├── components/               # Reusable UI components
│   │   ├── pages/                    # Page components (Login, Dashboard, Submit)
│   │   ├── api/                      # Axios API clients
│   │   ├── contexts/                 # React contexts (Auth, etc.)
│   │   └── theme/                    # Color palette & styling
│   └── vite.config.ts
│
├── Infra/                            # Terraform IaC
│   ├── main.tf                       # GCP infrastructure definitions
│   └── templates/                    # Init scripts, env templates
│
└── Prompts.personal/                 # AI prompts (project documentation)
```

---

## 🐳 Deployment

### **Docker Compose (Local)**
```bash
cd backend
docker-compose -f docker-build.sh up -d
```

### **Google Cloud (Production)**

1. **Build & Push Images**
   ```bash
   gcloud builds submit --config=cloudbuild.yaml
   ```

2. **Deploy to GKE/k3s**
   ```bash
   kubectl apply -f k8s-manifests/
   ```

3. **Terraform Infrastructure**
   ```bash
   cd Infra
   terraform plan
   terraform apply
   ```

---

## 📚 Resources

| Resource | Link |
|----------|------|
| **Demo Video** | [Google Drive Folder](https://drive.google.com/drive/folders/1tbesCiqKB--7RuhiqrSNAwkuEv1DuxQB) |
| **Architecture Diagrams** | [Excalidraw (Sequence, Bird's-eye, Runtime)](https://excalidraw.com/#room=b8ebf73a0c74ccbe6d63,snX8n1IqTTxGzJqEyxWntg) |
| **Design Document** | [Google Docs](https://docs.google.com/document/d/1toqy_5Xf8u-6sPL1vU56dWp3D7-in_jifFuzY1cdfHE/edit?tab=t.0) |

---

## 🔑 Key Design Decisions

### **GCP Authentication**
- **Application Default Credentials (ADC)** only — no service account key files
- Presigned URL signing uses explicit service account credentials
- Follows principle of least privilege with IAM roles

### **Concurrency**
- **Python**: `asyncio` for I/O-bound microservices (Pub/Sub, DB, GCS)
- **Go**: Channel-based pipeline architecture, zero mutexes on hot paths
- Prevents garbage collection pauses under extreme load (100K+ RPS)

### **Database**
- **SQLAlchemy 2.0** with async support for non-blocking database operations
- Timezone-aware timestamps (`DateTime(timezone=True)`)
- Connection pooling for Cloud SQL via Unix socket

### **Kubernetes**
- **In-cluster config** (`load_incluster_config()`) — no kubeconfig files
- **Kata Containers** for hardware-level isolation of contestant engines
- Status polling via Python Kubernetes client

### **Message Reliability**
- **Pub/Sub ACK**: Only after successful handoff to next stage
- **Lease extension**: Async tasks extend TTL during long-running operations
- **Dead-letter queues**: Failed submissions tracked for replay

---

## 🤝 Contributing

- Write tests for new features (pytest for Python, CTest for C++)
- Follow code style: Black for Python, clang-format for C++, gofmt for Go
- Document environment variable changes
- Update design docs for architectural changes

---

**Last Updated**: June 2026