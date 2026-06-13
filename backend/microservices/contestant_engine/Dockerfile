# Multi-stage build for Contestant Engine service
ARG BASE_IMAGE=ubuntu:24.04

FROM ${BASE_IMAGE} AS builder

WORKDIR /build

RUN apt-get update \
    && apt-get install -y --no-install-recommends --no-install-suggests \
        build-essential \
        cmake \
        ninja-build \
        libboost-system1.83-dev \
        libboost-thread1.83-dev \
        libboost-filesystem1.83-dev \
        libboost-regex1.83-dev \
        nlohmann-json3-dev \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

    
COPY . /src/contestant_engine
# (If your CMakeLists.txt relies on shared_core, uncomment the line below)
# COPY shared_core /src/shared_core

# FIX 2 & 3: Use cache mounts to save .o files, and limit concurrency to 4
RUN --mount=type=cache,target=/build/contestant_engine \
    cmake -S /src/contestant_engine -B /build/contestant_engine \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTS=OFF \
        -DBUILD_BENCHMARKS=OFF \
    && cmake --build /build/contestant_engine --target fix_gateway -j4

# FIX 4: Stage the binary outside the cache mount before moving to final image
RUN --mount=type=cache,target=/build/contestant_engine \
    cp /build/contestant_engine/fix_gateway /build/fix_gateway_binary

FROM ${BASE_IMAGE}

WORKDIR /app

RUN apt-get update \
    && apt-get install -y --no-install-recommends --no-install-suggests \
        libboost-system1.83.0 \
        libboost-thread1.83.0 \
        libboost-filesystem1.83.0 \
        libboost-regex1.83.0 \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# FIX 5: Pull staged binary and fix the config context path
COPY --from=builder /build/fix_gateway_binary /app/fix_gateway
COPY config /app/config

ENV PATH="/app:$PATH" \
    PYTHONUNBUFFERED=1

EXPOSE 8080 9876 9100 9101

CMD ["/app/fix_gateway"]