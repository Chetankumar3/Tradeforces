# 1. Update the base image to match your go.mod requirement
# Run from Tradeforces/backend folder
FROM golang:1.26.3-alpine AS build

WORKDIR /src

# Download dependencies first (caching optimization)
COPY go.mod go.sum ./
RUN go mod download

# Copy the rest of your flat folder structure
COPY . .

# 2. Build the current directory (.) since there is no cmd/ or services/ folder
RUN CGO_ENABLED=0 go build -o /out/benchmark-controller .

# Final minimal stage
FROM alpine:3.22
RUN apk add --no-cache ca-certificates

WORKDIR /app
COPY --from=build /out/benchmark-controller /app/benchmark-controller

EXPOSE 8080
CMD ["/app/benchmark-controller"]