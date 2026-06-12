package goroutines

import (
	"context"
	"fmt"
	"log"
	"math"
	"time"

	"github.com/iicpc/harness/internal/types"
	"github.com/twmb/franz-go/pkg/kgo"
)

// RunGo6 is the Scorer. On each tick it pulls a latency/throughput snapshot from
// Go4 and the correctness % from Go3 (both synchronous via embedded response
// channels), computes the weighted composite score, and publishes a JSON record
// to the Redpanda results topic keyed by SUBMISSION_ID.
func RunGo6(client *kgo.Client, cfg *types.Config, schemaFields map[string]string,
	scoreReqCh chan<- types.ScoreRequest,
	correctnessReqCh chan<- types.CorrectnessRequest,
	logger *log.Logger) {

	ticker := time.NewTicker(time.Duration(cfg.ScorerIntervalSec) * time.Second)
	defer ticker.Stop()

	publishCount := 0 // number of publishes so far; drives the time_lapse field

	for {
		<-ticker.C

		// Pull the snapshot from Go4 (synchronous: send request, block on response).
		snapRespCh := make(chan types.ScoreSnapshot, 1)
		scoreReqCh <- types.ScoreRequest{RespCh: snapRespCh}
		snap := <-snapRespCh

		// Pull correctness from Go3 (same embedded-channel pattern).
		corrRespCh := make(chan float64, 1)
		correctnessReqCh <- types.CorrectnessRequest{RespCh: corrRespCh}
		C := <-corrRespCh

		score := computeScore(snap, C, cfg)

		// Time lapse = (publishes so far) * window size. First publish 0s, then
		// one scorer interval per subsequent publish: 0, interval, 2*interval, ...
		timelapse := publishCount * cfg.ScorerIntervalSec
		payload := buildScoreJSON(snap, C, score, schemaFields, timelapse)

		// Publish to Redpanda; SUBMISSION_ID is the partition KEY, not a payload field.
		client.Produce(context.Background(), &kgo.Record{
			Topic: cfg.ResultsTopic,
			Key:   []byte(cfg.SubmissionID),
			Value: payload,
		}, nil) // async fire-and-forget

		publishCount++

		if debugEnabled {
			logger.Printf("Go6: published score=%.4f correctness=%.2f%% ack_p99=%dns timelapse=%ds",
				score, C, snap.ACK_P99, timelapse)
		}
	}
}

// computeScore applies the weighted composite formula:
//
//	(C/100)^x * [ W1*(B50/L50) + W2*(B90/L90) + W3*(B99/L99) + W4*(T/BT) ]
//
// where L50/L90/L99 are the contestant's ACK percentiles and T its peak TPS.
func computeScore(snap types.ScoreSnapshot, C float64, cfg *types.Config) float64 {
	correctnessFactor := math.Pow(C/100.0, cfg.CorrectnessExp)

	// Guard denominators against zero.
	p50 := max(snap.ACK_P50, 1)
	p90 := max(snap.ACK_P90, 1)
	p99 := max(snap.ACK_P99, 1)
	tps := max(snap.MaxThroughput, 1)

	latencyScore :=
		cfg.W1*(float64(cfg.BaselineP50NS)/float64(p50)) +
			cfg.W2*(float64(cfg.BaselineP90NS)/float64(p90)) +
			cfg.W3*(float64(cfg.BaselineP99NS)/float64(p99)) +
			cfg.W4*(float64(tps)/float64(cfg.BaselineThroughput))

	return correctnessFactor * latencyScore
}

// buildScoreJSON constructs the results record using the schemaFields map for
// output key names (so fields can be renamed without recompiling). No reflection.
func buildScoreJSON(snap types.ScoreSnapshot, C, score float64,
	schemaFields map[string]string, timelapse int) []byte {

	s := fmt.Sprintf(
		`{"%s":%d,"%s":%d,"%s":%d,"%s":%d,"%s":%d,"%s":%d,"%s":%d,"%s":%g,"%s":%g,"time_lapse_sec":%d}`,
		schemaFields["ack_p50_ns"], snap.ACK_P50,
		schemaFields["ack_p90_ns"], snap.ACK_P90,
		schemaFields["ack_p99_ns"], snap.ACK_P99,
		schemaFields["exec_p50_ns"], snap.EXEC_P50,
		schemaFields["exec_p90_ns"], snap.EXEC_P90,
		schemaFields["exec_p99_ns"], snap.EXEC_P99,
		schemaFields["max_throughput_rps"], snap.MaxThroughput,
		schemaFields["correctness_pct"], C,
		schemaFields["combined_score"], score,
		timelapse,
	)
	return []byte(s)
}
