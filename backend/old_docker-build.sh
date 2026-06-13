#!/bin/bash

set -euo pipefail

cd "$(dirname "$0")/.."
cd backend

# Manual single-service build & push commands (ready to copy/run directly)
docker build --progress=auto \
  -t "us-central1-docker.pkg.dev/project-cdd074dc-6291-4d7f-a2a/tradeforces/tradeforces-main:1.1" \
  -f microservices/main/Dockerfile . && \
  docker push "us-central1-docker.pkg.dev/project-cdd074dc-6291-4d7f-a2a/tradeforces/tradeforces-main:1.1" && \
  echo "Main service pushed successfully"

docker build --progress=auto \
  -t "us-central1-docker.pkg.dev/project-cdd074dc-6291-4d7f-a2a/tradeforces/tradeforces-vmcreator:1.1" \
  -f microservices/vm_creator/Dockerfile . && \
  docker push "us-central1-docker.pkg.dev/project-cdd074dc-6291-4d7f-a2a/tradeforces/tradeforces-vmcreator:1.1" && \
  echo "VM Creator service pushed successfully"

docker build --progress=auto \
  --build-arg BASE_IMAGE=ubuntu:24.04 \
  -t "us-central1-docker.pkg.dev/project-cdd074dc-6291-4d7f-a2a/tradeforces/tradeforces-shadow:1.1" \
  -f microservices/shadow_engine/Dockerfile \
  microservices/shadow_engine && \
  docker push "us-central1-docker.pkg.dev/project-cdd074dc-6291-4d7f-a2a/tradeforces/tradeforces-shadow:1.1" && \
  echo "Shadow Engine service pushed successfully"

docker build --progress=auto \
  -t "us-central1-docker.pkg.dev/project-cdd074dc-6291-4d7f-a2a/tradeforces/tradeforces-telemetry:1.1" \
  -f microservices/telemetry/Dockerfile microservices/telemetry && \
  docker push "us-central1-docker.pkg.dev/project-cdd074dc-6291-4d7f-a2a/tradeforces/tradeforces-telemetry:1.1" && \
  echo "Telemetry service pushed successfully"

sudo systemctl stop docker.service docker.socket