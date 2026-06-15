package goroutines

import (
	"context"
	"fmt"
	"log"
	"math"
	"strconv"
	"time"

	"github.com/iicpc/telemetry/internal/types"
	"github.com/redis/go-redis/v9"
)

// RunGo6 is the Scorer. On each tick it pulls a latency/throughput snapshot from
// Go4 and the correctness % from Go3, computes the weighted composite score,
// and publishes the ranking to a ZSET and detailed metrics to a HASH in Redis.
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

		// 4. Enforce 8 decimal precision for string conversion
		scoreStr := fmt.Sprintf("%.8f", score)
		corrStr := fmt.Sprintf("%.8f", C)

		// 5. Redis Publish (Strict 2-Second Timeout)
		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		identifier := cfg.SubmissionID
		zsetKey := "leaderboard2"
		hashKey := fmt.Sprintf("leaderboard2:data:%s", identifier)

		// Use the truncated score for both ZSET and HASH so the ranking value
		// exactly matches the displayed combined_score (no hidden micro-decimal
		// tie-breaks between teams shown with the same 8-decimal score).
		truncatedScore, err := strconv.ParseFloat(scoreStr, 64)
		if err != nil {
			logger.Printf("[ERROR] Failed to parse truncated score %q for %s: %v", scoreStr, identifier, err)
			cancel()
			continue
		}

		// Pipeline ZADD and HSET for efficient single round-trip execution.
		// Note: Pipeline (not TxPipeline) — commands are batched but NOT
		// wrapped in MULTI/EXEC, so readers may observe the ZSET and HASH
		// update in separate steps.
		pipe := rdb.Pipeline()

		// Push score to sorted set
		pipe.ZAdd(ctx, zsetKey, redis.Z{
			Score:  truncatedScore,
			Member: identifier,
		})

		// Store full metadata payload in HASH using schema mappings
		pipe.HSet(ctx, hashKey,
			schemaFields["ack_p50_ns"], snap.ACK_P50,
			schemaFields["ack_p90_ns"], snap.ACK_P90,
			schemaFields["ack_p99_ns"], snap.ACK_P99,
			schemaFields["exec_p50_ns"], snap.EXEC_P50,
			schemaFields["exec_p90_ns"], snap.EXEC_P90,
			schemaFields["exec_p99_ns"], snap.EXEC_P99,
			schemaFields["max_throughput_rps"], snap.MaxThroughput,
			schemaFields["correctness_pct"], corrStr,
			schemaFields["combined_score"], scoreStr,
		)

		// Execute pipeline
		if _, err := pipe.Exec(ctx); err != nil {
			logger.Printf("[ERROR] Redis Pipeline failed for %s: %v", identifier, err)
		} else {
			logger.Printf("[INFO] Pushed Score to Redis: %s | Submission: %s", scoreStr, identifier)
		}
		cancel() // Free resources immediately
		publishCount++
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