#!/bin/bash
REGISTRY="${1:-us-central1-docker.pkg.dev/project-cdd074dc-6291-4d7f-a2a/tradeforces}"
VERSION="${2:-1.1}"

set -euo pipefail

cd "$(dirname "$0")"
export DOCKER_BUILDKIT=1

build_and_push() {
  local service_name="$1"
  local image_name="$2"
  local dockerfile="$3"
  local context="$4"

  echo "Starting ${service_name} build/push"
  docker build --progress=auto \
    -t "${REGISTRY}/${image_name}:${VERSION}" \
    -f "${dockerfile}" \
    "${context}"

  docker push "${REGISTRY}/${image_name}:${VERSION}"
  echo "${service_name} pushed successfully"
}

build_and_push "Main service" "tradeforces-main" "microservices/main/Dockerfile" "." &
pid_main=$!

build_and_push "VM Creator service" "tradeforces-vmcreator" "microservices/vm_creator/Dockerfile" "." &
pid_vm=$!

build_and_push "Shadow Engine service" "tradeforces-shadow" "microservices/shadow_engine/Dockerfile" "microservices/shadow_engine" &
pid_shadow=$!

build_and_push "Telemetry service" "tradeforces-telemetry" "microservices/telemetry/Dockerfile" "microservices/telemetry" &
pid_telemetry=$!

status=0
for pid in "$pid_main" "$pid_vm" "$pid_shadow" "$pid_telemetry"; do
  if ! wait "$pid"; then
    status=1
  fi
done

if [ "$status" -ne 0 ]; then
  echo "One or more image builds/pushes failed" >&2
  exit 1
fi

sudo systemctl stop docker.service docker.socket