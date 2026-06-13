# IICPC HFT Evaluation telemetry

A production-grade Go evaluation telemetry for the IICPC hackathon scoring platform.
It consumes pre-serialized FIX 4.2 orders from Redpanda, streams them to a
contestant matching engine over TCP, reads execution reports from both the
contestant and a trusted shadow engine, measures nanosecond-precision latencies
with HDR histograms, validates correctness, and periodically publishes a
combined score back to a Redpanda results topic.

**Design constraints (all enforced):** target 100,000+ RPS, zero-allocation hot
path, **no mutexes anywhere**, channel-only concurrency (share memory by
communicating).

## Architecture — 7 goroutines

| Goroutine | Role | Owns exclusively |
|-----------|------|------------------|
| **Go1A** | Ingress consumer (Redpanda → memory queue) | — |
| **Go1B** | Ingress TCP writer (`syscall.Write` on raw fd, backpressure kill) | `stop_chan` (closed once via `sync.Once`) |
| **Go2**  | Egress reader — contestant (`bufio.Reader`, netpoller) | — |
| **Go7**  | Egress reader — shadow (blocking, trusted) | — |
| **Go4**  | State & latency manager (HDR histograms, throughput) | DS1, DS2, DS15, DS16, ACK/EXEC histograms |
| **Go3**  | Correctness worker (bidirectional report matching) | shadowPending, contestantPending, counters |
| **Go6**  | Scorer (weighted composite, publishes JSON) | — |

State that would normally need a lock is instead **owned by exactly one
goroutine**; everything else talks to it over channels (see `cmd/telemetry/main.go`
for the full channel plumbing and buffer sizes).

## Layout

```
cmd/telemetry/main.go         startup: env, loggers, kgo client, TCP dials, channels, launch
internal/types/types.go     all cross-goroutine structs + Config
internal/fix/parser.go      zero-alloc FIX 4.2 parsing (ParseTag, Atoi, ReadFIXMessage)
internal/goroutines/        globals.go (debugEnabled const) + go1a..go7
config/schema.json          output JSON field names (mounted at /etc/telemetry/schema.json)
Dockerfile                  multi-stage static build (CGO off, linux/amd64)
```

## Build & run

> **Requires Go 1.22+.** This repo ships `go.mod` but **not** `go.sum` — generate
> it (and download deps) before the first build:

```bash
go mod tidy        # generates go.sum and populates the module cache
go build ./...     # compile everything
```

> **Platform note:** Go1B uses `syscall.Write`, `syscall.EAGAIN`, and a raw
> socket fd — this is **Linux-specific** by design (matches the `GOOS=linux`
> Docker build). It will not compile on Windows/macOS natively. Build inside the
> container, or cross-compile: `GOOS=linux GOARCH=amd64 go build ./cmd/telemetry`.

### Docker

```bash
go mod tidy                       # must exist before docker build (Dockerfile COPYs go.sum)
docker build -t iicpc-telemetry .
docker run --rm --env-file .env iicpc-telemetry
```

### Configuration

All configuration is via environment variables — see `.env.example`. `main.go`
**panics immediately** if any variable is missing or fails to parse; no goroutine
ever reads the environment directly. Output JSON field names are remappable at
runtime by editing `config/schema.json` (mounted at `/etc/telemetry/schema.json`)
— no recompile needed.

## Scoring formula

```
Final Composite Score = (C/100)^x * [ W1·(B50/L50) + W2·(B90/L90) + W3·(B99/L99) + W4·(T/BT) ]
```

where `C` = correctness %, `x` = `SCORE_CORRECTNESS_EXP`, `L50/L90/L99` are the
contestant ACK-latency percentiles, `T` = peak TPS, and `B*`/`BT` are the
configured baselines. Go6 publishes the 9 schema fields plus `time_lapse_sec`
(publish index × scorer interval) to the results topic, keyed by `SUBMISSION_ID`.

## Debug logging

Each goroutine writes to its own file under `logs/` (`go1a.log` … `go7.log`).
Toggle via the package-level `const debugEnabled` in
`internal/goroutines/globals.go` — because it's a `const`, setting it to `false`
lets the compiler eliminate every debug branch at compile time (zero runtime cost).
