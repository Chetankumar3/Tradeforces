# Tradeforces — High-Frequency Trading Hackathon Platform

**A cloud-native, high-throughput evaluation system for algorithmic trading submissions with microsecond-precision latency measurement and correctness validation.**

---

## 📋 Table of Contents
- [Resources](#resources)
- [Project Overview](#project-overview)
- [Tech Stack](#tech-stack)
- [Architecture](#architecture)
- [Advanced Architectural Patterns](#advanced-architectural-patterns)
- [Deployment](#deployment)

---

---

## 📚 Resources

| Resource | Link |
|----------|------|
| **Demo Video** | [Google Drive Folder](https://drive.google.com/drive/folders/1tbesCiqKB--7RuhiqrSNAwkuEv1DuxQB) |
| **Architecture Diagrams** | [Excalidraw (Sequence, Bird's-eye, Go Runtime Flow)](https://excalidraw.com/#room=b8ebf73a0c74ccbe6d63,snX8n1IqTTxGzJqEyxWntg) |
| **Design Document** | [Google Docs](https://docs.google.com/document/d/1toqy_5Xf8u-6sPL1vU56dWp3D7-in_jifFuzY1cdfHE/edit?tab=t.0) |

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
- **Google Compute Engine**: Hardware Virtualization Enabled Compute Engines for sandboxing the user's code.
- **Google Cloud Pub/Sub**: Message queuing for async handoffs between microservices
- **Google Cloud Build**: Container image building from submission source
- **Google Cloud Storage (GCS)**: Submission storage with V4 presigned URLs
- **Google Cloud Artifact Registry**: Docker image registry for contestant engines
- **Google Cloud SQL**: PostgreSQL database (async SQLAlchemy 2.0)

### **Kubernetes & Containerization**
- **Kubernetes (k3s)**: Lightweight orchestration cluster
- **Kata Containers**: Hardware-isolated microVMs for contestant engines
- **QEMU Hypervisor**: Hardware-isolated microVMs for contestant engines
- **Docker**: Containerization for all microservices

### **Message Streaming**: 
- **Redpanda**: Kafka-compatible event broker for order ingestion and score publishing

### **Backend**
- **Python**
- **Go**
- **Postgres**

### **Frontend**
- **React 18 + TypeScript**: Single-page application (SPA)
- **TanStack Query**: Server state management
- **Tailwind CSS**: Utility-first styling

---

## 🏗️ Architecture

### **Seven Microservices**

#### **1. Main API Gateway (`main` — Python FastAPI)**
Entrypoint for team submissions. Handles JWT authentication, issues presigned GCS upload URLs, and triggers the build pipeline.
- Endpoint: `POST /login/credentials` — User login
- Endpoint: `GET /submit/{submission_id}` — Get presigned V4 GCS upload URL
- Endpoint: `POST /upload_complete/{submission_id}` — Mark submission complete and trigger build
- Publishes metadata to **Pub/Sub Queue 1** with FIX-serialized order parameters
- Updates PostgreSQL with status tracking

#### **2. Async Build & Deploy Orchestrator (`vm_creator` — Python FastAPI)**
Orchestrates the complete evaluation environment for each submission.
- Consumes from **Pub/Sub Queue 1**
- Triggers **Google Cloud Build** to compile submission ZIP into Docker image
- Pushes image to **Google Cloud Artifact Registry**
- **Creates Execution Triad with Pod Affinity** (colocated on same node):
  - **Contestant Engine Pod** (Kata microVM running compiled submission)
  - **Shadow Engine Pod** (reference implementation for correctness validation)
  - **Telemetry Service Pod** (Go measurement harness)
- **Creates dedicated Redpanda topic** for order stream (single partition, FIFO guarantee)
- Publishes execution metadata to **Pub/Sub Queue 2**
- Implements backpressure monitoring: stops pulling Q1 if Q2 depth exceeds threshold
- ACKs messages only after successful handoff to avoid reprocessing
- **Cleanup**: LoadGen controller deletes all 3 pods + Redpanda topic after test completion

#### **3. Shadow Reference Engine (`shadow_engine` — C++)**
Trusted reference implementation for correctness validation. Based on [Prateek Bala's Fix-Protocol-Trading-Engine](https://github.com/Prateekbala/Fix-Protocol-Tranding-Engine).
- Deployed as Pod by `vm_creator` (part of Exhibition Triad)
- Receives same order stream as contestant engine via dedicated Redpanda topic
- Executes matching logic independently
- Sends execution reports to Telemetry service over TCP
- Guarantees correctness baseline for comparison
- Pod affinity: colocated on same node as contestant engine & telemetry for microsecond latencies

#### **4. Telemetry & Scoring Harness (`telemetry` — Go)**
The evaluation engine. Orchestrates order delivery, measures latencies (nanosecond precision), validates correctness, and computes final scores.

**7 Goroutine Pipeline Architecture:**
- **Go1A (Ingress Consumer)**: Polls **Redpanda** topic for pre-serialized FIX orders using franz-go
- **Go1B (Ingress TCP Writer)**: Writes order bytes to contestant engine over persistent TCP; captures `time.Now().UnixNano()` post-syscall
- **Go2 (Egress TCP Reader)**: Reads contestant execution reports from TCP; parses OrdID, ExecID, Aggressor flag
- **Go3 (Correctness Validator)**: Compares contestant exec reports vs. shadow engine output; maintains correctness score
- **Go4 (Latency Manager)**: Single source of truth for all state (no locks, channel-only); manages HDR histograms, throughput tracking
- **Go6 (Scorer)**: Ticker-based loop; pulls latency percentiles from Go4, correctness from Go3, publishes composite score to Redpanda results topic
- **Go7 (Shadow Engine Reader)**: Reads reference engine output; sends to Go3 for comparison

Uses **zero-allocation hot path** design with preallocated buffers and lock-free channels.

#### **5. Load Generation Controller (`loadgen/controller` — Python)**
Control plane for distributed load generation. Orchestrates when and how many orders to inject.
- Monitors Q2 (Ready for Load) depth
- Pulls contestant Pod IPs only when cluster has free vCPUs
- Generates FIX-serialized order packets
- Publishes orders to Redpanda ingress topic (single partition for FIFO guarantee)

#### **6. Load Generation Data Plane (`loadgen/runner` — Distributed Go/Python pods)**
Data plane workers that push orders at extreme rates (100K+ RPS target).
- Receives orders from Redpanda via Pull subscriptions
- Routes orders to contestant engines via TCP load balancing
- Reports throughput metrics back to controller
- Auto-scales based on Pod resource utilization

#### **7. Dashboard & Metrics (`extra_services/dashpusher` — Python FastAPI)**
Side service for real-time leaderboard and metrics visualization.
- Consumes scores from Redpanda results topic
- Pushes live updates to frontend via WebSocket
- Serves team rankings, latency histograms, correctness scores
- Integration with PostgreSQL for persistent score history

### **Data Flow**

```
Team Submission (Frontend)
  ↓
[Main Gateway] → GCS Upload (presigned URL)
  ↓
[Main Gateway] → Pub/Sub Queue 1 (submission metadata, FIX serialized)
  ↓
[VM Creator] → Google Cloud Build → Artifact Registry
  ↓
[VM Creator] → Creates Exhibition Triad (pod affinity colocation):
              - Contestant Engine Pod (Kata microVM)
              - Shadow Engine Pod (reference)
              - Telemetry Service Pod (measurement harness)
              - Dedicated Redpanda Topic (single partition, FIFO)
  ↓
[VM Creator] → Pub/Sub Queue 2 (exhibition metadata)
  ↓
[LoadGen Controller] ← Q2 (pulls exhibition when cluster has free vCPU)
  ↓
[LoadGen Controller] → Publishes orders to Exhibition's Redpanda topic
  ↓
[Telemetry] ← Redpanda Orders (Go1A pulls from dedicated topic)
  ↓
[Telemetry] --TCP--> [Contestant Engine] + [Shadow Engine] (Go1B/Go2/Go7)
  ↓
[Telemetry] HDR Histogram (latency) + Correctness Validation (Go4/Go3)
  ↓
[Telemetry] → Redpanda Results Topic (score)
  ↓
[DashPusher] → Frontend WebSocket (live leaderboard)
  ↓
[LoadGen Controller] → Deletes Exhibition Triad Pods + Redpanda Topic (after test)
```

---

## 🔑 Advanced Architectural Patterns

### **1. Ultimate Sandboxing with MicroVMs & CPU Isolation**
**Challenge**: Contestant engines must run with complete resource isolation to prevent interference and ensure fair benchmarking.

**Solution**: Hardware-isolated **Kata Containers** (QEMU-backed microVMs) with **CPU pinning** and **CPU set isolation**.
- Each contestant engine Pod gets dedicated vCPU cores (no sharing via CFS quota)
- MicroVM creation: 200-300ms overhead (acceptable for single submission but amortized across contest)
- Zero kernel cross-talk: each engine has its own kernel, memory, and namespace
- Guarantees deterministic latency profiles for fair scoring

### **2. Two-Queue (2Q) Backpressure Architecture**
**Challenge**: At 100K+ RPS, if Queue 1 (Submissions) pushes microVMs faster than load-gen pods can consume them, idle sandboxes exhaust cluster CPU/RAM.

**Solution**: Split the pipeline into **Queue 1 (Pending Submissions)** and **Queue 2 (Ready for Load)** with intelligent pull-based semantics.
- **VM Creator** pulls from Q1, boots microVM, pushes Pod IP to Q2
- **VM Creator monitors Q2 depth**: if `Q2_backlog > threshold`, it **stops pulling from Q1** entirely
- **LoadGen pods pull from Q2 only when they have free vCPUs**—they never pull more work than they can immediately execute
- Result: Self-regulating cascade that prevents resource starvation

### **3. Redpanda Single-Partition Sequencing (Mocking the FPGA)**
**Challenge**: In real exchanges, hardware FPGAs sequence packets at the switch. At 100K+ RPS with 1000+ submissions, pre-sequencing at load-gen level causes packet reordering due to network jitter, unfairly penalizing contestants.

**Solution**: Offload the "Absolute Timeline" to **Redpanda with exactly one partition**.
- **Redpanda guarantees strict FIFO** for single-partition topics—no reordering at any scale
- Orders pulled by telemetry harness are **perfectly sequenced** and globally ordered
- Contestants can't gain advantage through network scheduling tricks
- Software gateway (Telemetry) becomes stateless: simply pull and route immutable stream

### **4. Bypassing gRPC: Raw TCP for the Hot Path**
**Challenge**: gRPC (HTTP/2) introduces multiplexing overhead, header compression, and connection management—unacceptable for latency-sensitive scoring.

**Solution**: Drop gRPC entirely from critical path; use **raw TCP sockets with FIX protocol byte-streams**.
- Telemetry connects to contestant engine via persistent raw TCP (no HTTP handshake)
- Orders arrive as FIX-serialized bytes; engines parse field delimiters (`\x01`) directly from memory
- Execution reports streamed back the same way—no JSON parsing, no protocol negotiation
- **Result**: Sub-microsecond latency measurement without application-layer bloat
- Redpanda remains the high-level orchestration layer for ordering guarantees

### **5. Pod Affinity for the Execution Triad**
**Challenge**: Even optimized code shows poor latency if contestant, shadow, and telemetry pods are on different physical nodes (packets cross datacenter switches).

**Solution**: Hardcoded **Kubernetes Pod Affinity** scheduling.
- When `vm_creator` deploys a submission:
  - Contestant Engine Pod (Kata microVM)
  - Shadow Engine Pod (reference implementation)
  - Telemetry Service Pod (Go measurement harness)
- All three scheduled on the **same physical worker node**
- Network traffic stays in-kernel via virtual bridge → microsecond-fast memory layer
- No NIC traversal, no external switch involvement

### **6. 7-Goroutine Pipeline Architecture in Telemetry**
**Challenge**: Measuring 100K+ orders/second while computing HDR histograms, managing state, and handling backpressure requires extreme concurrency without mutexes (GC pauses kill latency).

**Solution**: Channel-based goroutine pipeline ("share memory by communicating").
- **Go1A** → **Go1B** → **IngressEvent** channel → **Go4** (no contention)
- **Go2** → **EgressEvent** channel → **Go4** (same)
- **Go3** → **Correctness** channel (independent)
- **Go4**: Single source of truth; select loop binds all channels, processes events, updates histograms (no locks)
- **Go6**: Ticker-based scorer; requests snapshots from Go4/Go3 via synchronous request-response channels
- **Result**: Lock-free state management, zero allocations on hot path, sub-microsecond tail latencies

### **7. FIX Protocol for Message Serialization**
**Challenge**: JSON and Protocol Buffers add parse overhead and allocations in tight loops; need compact, pre-defined binary format.

**Solution**: **FIX 4.2 (Financial Information Exchange) protocol**.
- Orders serialized as: `Tag=Value\x01` byte strings
- Tag 11 = ClOrdID, Tag 55 = Symbol, Tag 54 = Side, Tag 38 = OrderQty, etc.
- Engines parse by scanning for `\x01` delimiters—no JSON parser, no schema negotiation
- Standard in HFT/exchanges; minimal parsing logic = minimal latency
- Redpanda and LoadGen use same format end-to-end for zero translation overhead

### **8. Cloud Build for Runtime Image Creation**
**Challenge**: Cannot require teams to build Docker images locally; need reproducible, secure builds in cloud.

**Solution**: **Google Cloud Build** triggered on submission upload.
- Team submits ZIP archive with source + Dockerfile
- Cloud Build clones the Dockerfile from ZIP, builds in hermetic environment
- Image tagged with `submission_id:latest` and pushed to **Artifact Registry**
- **VM Creator** pulls the immutable image reference and deploys immediately
- Build logs retained for debugging failed submissions

---

---

## 🏆 Acknowledgments

**Prateek Bala** — The Shadow (reference) Trading Engine is based on [Prateek Bala's Fix-Protocol-Trading-Engine](https://github.com/Prateekbala/Fix-Protocol-Tranding-Engine). His open-source implementation provided the foundation for our correctness validation harness and influenced the architecture of our distributed evaluation system.

---

**Last Updated**: June 2026