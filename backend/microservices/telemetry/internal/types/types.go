// Package types holds all cross-goroutine message structs and the parsed Config.
// These are the only values that travel over channels between goroutines.
package types

// OrderMessage is parsed by Go1A from Redpanda and consumed by Go1B.
type OrderMessage struct {
	OrdID    string // parsed from Tag 11 (ClOrdID)
	RawBytes []byte // full FIX message bytes, copied from the Redpanda record
}

// IngressEvent is sent from Go1B to Go4 after writing an order to the TCP socket.
type IngressEvent struct {
	OrdID    string
	TIngress int64 // time.Now().UnixNano() captured immediately after syscall.Write
}

// EgressEvent is sent from Go2 to Go4 after reading an exec report from the contestant.
type EgressEvent struct {
	OrdID     string // Tag 11 ClOrdID of this report's own order
	MatchID   int    // Tag 880 TrdMatchID - groups an aggressor's fill with the resting fills it cleared
	Aggressor bool   // true if Tag 1057 == 'Y'
	ArrTime   int64  // time.Now().UnixNano() captured immediately after ReadFIXMessage
}

// ScoreRequest is sent by Go6 to Go4. It embeds a buffered response channel so
// Go6 can block on the reply synchronously without any shared state.
type ScoreRequest struct {
	RespCh chan ScoreSnapshot
}

// ScoreSnapshot is Go4's synchronous response to a ScoreRequest.
type ScoreSnapshot struct {
	ACK_P50       int64
	ACK_P90       int64
	ACK_P99       int64
	EXEC_P50      int64
	EXEC_P90      int64
	EXEC_P99      int64
	MaxThroughput int
}

// CorrectnessRequest is sent by Go6 to Go3. Same embedded-channel pattern.
type CorrectnessRequest struct {
	RespCh chan float64 // Go3 responds with correctness % (0.0-100.0)
}

// Config holds all parsed env vars. A *Config is passed to every goroutine.
type Config struct {
	RedpandaBrokers  string
	RPUser           string
	RPPass           string
	OrdersTopic      string
	IngressAddr      string
	EgressAddr       string
	ShadowEgressAddr string

	ScorerIntervalSec int
	SubmissionID      string

	W1, W2, W3, W4 float64
	CorrectnessExp float64

	BaselineP50NS      int64
	BaselineP90NS      int64
	BaselineP99NS      int64
	BaselineThroughput int
}
