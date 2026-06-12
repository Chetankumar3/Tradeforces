FROM golang:1.25-alpine AS build

WORKDIR /src

COPY go.mod go.sum ./
RUN go mod download

COPY . .
RUN CGO_ENABLED=0 go build -o /out/benchmark-controller ./services/benchmark-controller

FROM alpine:3.22
RUN apk add --no-cache ca-certificates

WORKDIR /app
COPY --from=build /out/benchmark-controller /app/benchmark-controller

EXPOSE 8080
CMD ["/app/benchmark-controller"]
