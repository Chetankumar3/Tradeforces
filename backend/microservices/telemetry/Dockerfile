# Run from Tradeforces/backend folder
FROM golang:1.22-alpine AS builder
WORKDIR /app
COPY go.mod go.sum ./
RUN go mod download
COPY . ./
RUN CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -ldflags="-s -w" -o telemetry ./cmd/telemetry

FROM alpine:3.19
RUN apk --no-cache add ca-certificates tzdata
WORKDIR /app
COPY --from=builder . /app/telemetry
RUN mkdir -p /etc/telemetry
COPY config/schema.json /etc/telemetry/schema.json
CMD ["/app/telemetry"]
