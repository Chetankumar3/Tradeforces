//go:build ignore

package goroutines

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"math"
	"time"

	"github.com/iicpc/telemetry/internal/types"
	"github.com/redis/go-redis/v9"
)

// RunGo6 is the Scorer. On each tick it pulls a latency/throughput snapshot from
// Go4 and the correctness % from Go3 (both synchronous via embedded response
// channels), computes the weighted composite score, and publishes a JSON record
// to the Redpanda results topic keyed by SUBMISSION_ID.
func RunGo6(
	rdb *redis.Client, 
	cfg *types.Config, 
	schemaFields map[string]string,
	scoreReqCh chan<- types.ScoreRequest,
	correctnessReqCh chan<- types.CorrectnessRequest,
	logger *log.Logger,
) {
	ticker := time.NewTicker(time.Duration(cfg.ScorerIntervalSec) * time.Second)
	defer ticker.Stop()

	publishCount := 0

	for {
		<-ticker.C

		// 1. Pull Snapshot (Go4)
		snapRespCh := make(chan types.ScoreSnapshot, 1)
		scoreReqCh <- types.ScoreRequest{RespCh: snapRespCh}
		snap := <-snapRespCh

		// 2. Pull Correctness (Go3)
		corrRespCh := make(chan float64, 1)
		correctnessReqCh <- types.CorrectnessRequest{RespCh: corrRespCh}
		C := <-corrRespCh

		// 3. Compute Math
		score := computeScore(snap, C, cfg)
		timelapse := publishCount * cfg.ScorerIntervalSec

		// 4. Build JSON Payload
		scoreData := map[string]interface{}{
			"team_id":            cfg.SubmissionID, 
			"submission_id":      cfg.SubmissionID,
			"composite_score":    score,
			"correctness_pct":    C,
			"ack_p50_ns":         snap.ACK_P50,
			"ack_p90_ns":         snap.ACK_P90,
			"ack_p99_ns":         snap.ACK_P99,
			"exec_p50_ns":        snap.EXEC_P50,
			"exec_p90_ns":        snap.EXEC_P90,
			"exec_p99_ns":        snap.EXEC_P99,
			"max_throughput_rps": snap.MaxThroughput,
			"time_lapse_sec":     timelapse,
		}

		jsonPayload, err := json.Marshal(scoreData)
		if err != nil {
			logger.Printf("[ERROR] Failed to marshal score JSON: %v", err)
			continue
		}

		// 5. Redis Publish (Strict 2-Second Timeout)
		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)

		redisKey := "leaderboard:composite"
		identifier := cfg.SubmissionID 

		// Push score to sorted set
		if err := rdb.ZAdd(ctx, redisKey, redis.Z{
			Score:  score,
			Member: identifier,
		}).Err(); err != nil {
			logger.Printf("[ERROR] Redis ZADD failed for %s: %v", identifier, err)
		} else {
			logger.Printf("[INFO] Pushed Score to Redis: %.2f | Submission: %s", score, identifier)
		}

		// Store full metadata payload
		setKey := fmt.Sprintf("score:%s", identifier)
		if err := rdb.Set(ctx, setKey, string(jsonPayload), 0).Err(); err != nil {
			logger.Printf("[ERROR] Redis SET failed for %s: %v", setKey, err)
		}
		
		cancel() // Free resources immediately

		publishCount++
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
