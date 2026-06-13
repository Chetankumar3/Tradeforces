# Multi-stage build for Shadow Engine service
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

RUN cmake -S /src/contestant_engine -B /build/contestant_engine \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTS=OFF \
        -DBUILD_BENCHMARKS=OFF \
    && cmake --build /build/contestant_engine --target fix_gateway -j"$(nproc)"

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

COPY --from=builder /build/contestant_engine/fix_gateway /app/fix_gateway
COPY config /app/config

ENV PATH="/app:$PATH" \
    PYTHONUNBUFFERED=1

EXPOSE 8080 9876 9100 9101

CMD ["/app/fix_gateway"]
