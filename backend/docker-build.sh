#!/bin/bash
REGISTRY="${1:-us-central1-docker.pkg.dev/project-cdd074dc-6291-4d7f-a2a/tradeforces}"
VERSION="${2:-1.1}"
SHADOW_BASE_IMAGE="${SHADOW_BASE_IMAGE:-ubuntu:24.04}"

set -euo pipefail

cd "$(dirname "$0")"
export DOCKER_BUILDKIT=1

build_and_push() {
  local service_name="$1"
  local image_name="$2"
  local dockerfile="$3"
  local context="$4"

  echo "Starting ${service_name} build/push"
  local build_args=()

  if [ "$service_name" = "Shadow Engine service" ]; then
    build_args+=(--build-arg BASE_IMAGE="${SHADOW_BASE_IMAGE}")
  fi

  DOCKER_BUILDKIT=1 docker build --progress=auto \
    "${build_args[@]}" \
    -t "${REGISTRY}/${image_name}:${VERSION}" \
    -f "${dockerfile}" \
    "${context}" && \
  docker push "${REGISTRY}/${image_name}:${VERSION}" && \
  echo "${service_name} pushed successfully"
}

build_and_push "Main service" "tradeforces-main" "microservices/main/Dockerfile" "." &
pid_main=$!

build_and_push "VM Creator service" "tradeforces-vmcreator" "microservices/vm_creator/Dockerfile" "." &
pid_vm=$!

build_and_push "Benchmark Controller service" "tradeforces-benchmark-controller" "microservices/loadgen/controller/Dockerfile" "microservices/loadgen/controller" &
pid_benchmark=$!

build_and_push "Bot Runner service" "tradeforces-bot-runner" "microservices/loadgen/runner/Dockerfile" "microservices/loadgen/runner" &
pid_bot_runner=$!

build_and_push "Shadow Engine service" "tradeforces-shadow" "microservices/shadow_engine/Dockerfile" "microservices/shadow_engine" &
pid_shadow=$!

build_and_push "Telemetry service" "tradeforces-telemetry" "microservices/telemetry/Dockerfile" "microservices/telemetry" &
pid_telemetry=$!

build_and_push "Contestant Engine service" "tradeforces-contestant" "microservices/contestant_engine/Dockerfile" "microservices/contestant_engine" &
pid_contestant=$!

status=0
for pid in "$pid_main" "$pid_vm" "$pid_benchmark" "$pid_bot_runner" "$pid_shadow" "$pid_telemetry" "$pid_contestant"; do
  if ! wait "$pid"; then
    status=1
  fi
done

if [ "$status" -ne 0 ]; then
  echo "One or more image builds/pushes failed" >&2
  exit 1
fi

sudo systemctl stop docker.service docker.socket