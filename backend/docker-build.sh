#!/bin/bash
REGISTRY="${1:-us-central1-docker.pkg.dev/project-cdd074dc-6291-4d7f-a2a/tradeforces}"
VERSION="${2:-1.1}"

cd "$(dirname "$0")/.."

cd backend
docker build -t "${REGISTRY}/tradeforces-main:${VERSION}" -f microservices/main/Dockerfile ../
docker push "${REGISTRY}/tradeforces-main:${VERSION}"
echo "Main service pushed successfully"

# cd ../vm_creator
docker build -t "${REGISTRY}/tradeforces-vmcreator:${VERSION}" -f microservices/vm_creator/Dockerfile ../
docker push "${REGISTRY}/tradeforces-vmcreator:${VERSION}"
echo "VM Creator service pushed successfully"

sudo systemctl stop docker.service docker.socket