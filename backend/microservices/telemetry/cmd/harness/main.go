// Command harness is the IICPC HFT evaluation harness. It consumes FIX 4.2
// orders from Redpanda, streams them to a contestant matching engine over TCP,
// reads contestant and shadow execution reports, measures nanosecond latencies,
// validates correctness, and periodically publishes a composite score back to
// Redpanda. See the master prompt for the full architecture.
package main

import (
	"crypto/tls"
	"encoding/json"
	"log"
	"net"
	"os"
	"strconv"
	"strings"

	"github.com/iicpc/harness/internal/goroutines"
	"github.com/iicpc/harness/internal/types"
	"github.com/twmb/franz-go/pkg/kgo"
	"github.com/twmb/franz-go/pkg/sasl/scram"
)

// schemaPath is the hardcoded container path for the output-field schema.
const schemaPath = "/etc/harness/schema.json"

func main() {
	// 1. Parse all env vars into *Config (panics on any missing/invalid value).
	cfg := loadConfig()

	// 2. Create logs/ and one *log.Logger per goroutine, each to its own file.
	if err := os.MkdirAll("logs", 0755); err != nil {
		panic(err)
	}
	logGo1A := newLogger("logs/go1a.log")
	logGo1B := newLogger("logs/go1b.log")
	logGo2 := newLogger("logs/go2.log")
	logGo3 := newLogger("logs/go3.log")
	logGo4 := newLogger("logs/go4.log")
	logGo6 := newLogger("logs/go6.log")
	logGo7 := newLogger("logs/go7.log")

	// 3. Read and JSON-decode the schema into map[string]string (output field names).
	schemaFields := loadSchema(schemaPath)

	// 4. Build the single shared, goroutine-safe franz-go client (Go1A + Go6).
	opts := []kgo.Opt{
		kgo.SeedBrokers(strings.Split(cfg.RedpandaBrokers, ",")...),
		kgo.DialTLSConfig(new(tls.Config)),
		kgo.SASL(scram.Auth{User: cfg.RPUser, Pass: cfg.RPPass}.AsSha256Mechanism()),
		kgo.ConsumeTopics(cfg.OrdersTopic),
		kgo.ConsumeResetOffset(kgo.NewOffset().AtStart()),
	}
	client, err := kgo.NewClient(opts...)
	if err != nil {
		panic(err)
	}

	// 5. Dial the contestant ingress connection (Go1B writes orders here).
	ingressConn := dialTCP(cfg.IngressAddr)

	// 6. Extract the raw fd for Go1B's syscall.Write. ingressConn is passed to
	//    Go1B as a parameter to keep it alive and prevent GC invalidating the fd.
	var ingressFD uintptr
	rawConn, err := ingressConn.SyscallConn()
	if err != nil {
		panic(err)
	}
	if err := rawConn.Control(func(fd uintptr) { ingressFD = fd }); err != nil {
		panic(err)
	}

	// 7 & 8. Dial the contestant egress and shadow egress connections.
	egressConn := dialTCP(cfg.EgressAddr)
	shadowConn := dialTCP(cfg.ShadowEgressAddr)

	// 9. Initialize all channels (types and buffer sizes per the plumbing spec).
	memoryQueueChan := make(chan types.OrderMessage, 10000)
	stopChan := make(chan struct{})
	ingressChan := make(chan types.IngressEvent, 10000)
	egressChan := make(chan types.EgressEvent, 10000)
	go3CorrectnessChan := make(chan []byte, 5000)
	shadowEgressChan := make(chan []byte, 5000)
	scoreRequestChan := make(chan types.ScoreRequest, 1)
	correctnessRequestChan := make(chan types.CorrectnessRequest, 1)

	// 10. Launch all goroutines.
	go goroutines.RunGo1A(client, cfg, memoryQueueChan, stopChan, logGo1A)
	go goroutines.RunGo1B(ingressFD, ingressConn, memoryQueueChan, ingressChan, stopChan, logGo1B)
	go goroutines.RunGo2(egressConn, egressChan, go3CorrectnessChan, stopChan, logGo2)
	go goroutines.RunGo7(shadowConn, shadowEgressChan, logGo7)
	go goroutines.RunGo4(ingressChan, egressChan, scoreRequestChan, logGo4)
	go goroutines.RunGo3(go3CorrectnessChan, shadowEgressChan, correctnessRequestChan, logGo3)
	go goroutines.RunGo6(client, cfg, schemaFields, scoreRequestChan, correctnessRequestChan, logGo6)

	// 11. Block main forever. Pod deletion handles cleanup.
	select {}
}

// newLogger opens (create/append) a log file and wraps it in a *log.Logger.
func newLogger(path string) *log.Logger {
	f, err := os.OpenFile(path, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0644)
	if err != nil {
		panic(err)
	}
	return log.New(f, "", log.LstdFlags|log.Lmicroseconds)
}

// dialTCP dials a TCP address and asserts to *net.TCPConn (needed for SyscallConn).
func dialTCP(addr string) *net.TCPConn {
	conn, err := net.Dial("tcp", addr)
	if err != nil {
		panic(err)
	}
	tcp, ok := conn.(*net.TCPConn)
	if !ok {
		panic("dialTCP: expected *net.TCPConn for " + addr)
	}
	return tcp
}

// loadSchema reads and JSON-decodes the output-field schema map.
func loadSchema(path string) map[string]string {
	data, err := os.ReadFile(path)
	if err != nil {
		panic(err)
	}
	var m map[string]string
	if err := json.Unmarshal(data, &m); err != nil {
		panic(err)
	}
	return m
}

// loadConfig parses every required env var into a *Config. Panics on the first
// missing or malformed value. No goroutine ever calls os.Getenv directly.
func loadConfig() *types.Config {
	return &types.Config{
		RedpandaBrokers:  mustStr("REDPANDA_BROKERS"),
		RPUser:           mustStr("REDPANDA_USER"),
		RPPass:           mustStr("REDPANDA_PASS"),
		OrdersTopic:      mustStr("REDPANDA_ORDERS_TOPIC"),
		ResultsTopic:     mustStr("REDPANDA_RESULTS_TOPIC"),
		IngressAddr:      mustStr("CONTESTANT_INGRESS_ADDR"),
		EgressAddr:       mustStr("CONTESTANT_EGRESS_ADDR"),
		ShadowEgressAddr: mustStr("SHADOW_EGRESS_ADDR"),

		ScorerIntervalSec: mustInt("SCORER_INTERVAL_SEC"),
		SubmissionID:      mustStr("SUBMISSION_ID"),

		W1: mustFloat("SCORE_W1"),
		W2: mustFloat("SCORE_W2"),
		W3: mustFloat("SCORE_W3"),
		W4: mustFloat("SCORE_W4"),

		CorrectnessExp: mustFloat("SCORE_CORRECTNESS_EXP"),

		BaselineP50NS:      mustInt64("BASELINE_P50_NS"),
		BaselineP90NS:      mustInt64("BASELINE_P90_NS"),
		BaselineP99NS:      mustInt64("BASELINE_P99_NS"),
		BaselineThroughput: mustInt("BASELINE_THROUGHPUT"),
	}
}

// --- env parsing helpers: panic immediately on missing/invalid values ---

func mustStr(key string) string {
	v, ok := os.LookupEnv(key)
	if !ok || v == "" {
		panic("missing required env var: " + key)
	}
	return v
}

func mustInt(key string) int {
	n, err := strconv.Atoi(mustStr(key))
	if err != nil {
		panic("invalid int for env var " + key + ": " + err.Error())
	}
	return n
}

func mustInt64(key string) int64 {
	n, err := strconv.ParseInt(mustStr(key), 10, 64)
	if err != nil {
		panic("invalid int64 for env var " + key + ": " + err.Error())
	}
	return n
}

func mustFloat(key string) float64 {
	f, err := strconv.ParseFloat(mustStr(key), 64)
	if err != nil {
		panic("invalid float64 for env var " + key + ": " + err.Error())
	}
	return f
}
