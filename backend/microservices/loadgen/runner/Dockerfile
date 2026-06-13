# Run from Tradeforces/backend folder
FROM golang:1.26.3-alpine AS build

WORKDIR /src

COPY go.mod go.sum ./
RUN go mod download

COPY . .
# Changed ./services/bot-runner to .
RUN CGO_ENABLED=0 go build -o /out/bot-runner .

FROM alpine:3.22
RUN apk add --no-cache ca-certificates

WORKDIR /app
COPY --from=build /out/bot-runner /app/bot-runner

CMD ["/app/bot-runner"]