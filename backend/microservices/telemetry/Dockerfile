# Run from Tradeforces/backend folder
FROM golang:1.24-alpine AS builder

# 1. Setup environment variables
ENV CGO_ENABLED=0 GOOS=linux GOARCH=amd64
WORKDIR /src

# 2. Copy ONLY the telemetry go.mod and go.sum first to utilize layer caching
COPY go.mod go.sum ./

# 3. Download dependencies using BuildKit's Go module cache
RUN --mount=type=cache,target=/go/pkg/mod \
    go mod download

# 4. Copy ONLY the telemetry service code (bypasses the heavy monorepo bloat)
COPY . .

# 5. Build the binary using the Go build cache. 
# We explicitly output to /out/telemetry-bin to prevent directory naming collisions.
RUN --mount=type=cache,target=/go/pkg/mod \
    --mount=type=cache,target=/root/.cache/go-build \
    go build -ldflags="-s -w" -o /out/telemetry-bin ./cmd/telemetry


# Final runtime stage
FROM alpine:3.19
RUN apk --no-cache add ca-certificates tzdata
WORKDIR /app

# 6. Pull EXACTLY the compiled binary file (no source code folders)
COPY --from=builder /out/telemetry-bin /app/telemetry-bin

# 7. Use the correct relative monorepo path for the config copy
RUN mkdir -p /etc/telemetry
COPY config/schema.json /etc/telemetry/schema.json

# Telemetry is a TCP client to the contestant/shadow engines and to Redpanda.
EXPOSE 9092 9100 9101

# 8. Execute the distinctly named binary
ENTRYPOINT ["/app/telemetry-bin"]
CMD []