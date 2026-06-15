package goroutines

import (
	"context"
	"log"
	"time"

	"github.com/iicpc/telemetry/internal/fix"
	"github.com/iicpc/telemetry/internal/types"
	"github.com/twmb/franz-go/pkg/kgo"
)

// RunGo1A is the Ingress Consumer. It consumes pre-serialized FIX 4.2 orders
// from Redpanda, parses OrdID (Tag 11), copies the raw bytes, and pushes an
// OrderMessage onto memoryCh for Go1B to write to the contestant engine.
//
// Phase 1 polls with a 3s timeout in a retry loop until the first batch arrives.
// Phase 2 polls continuously with a 100ms timeout so stop_chan is checked often.
func RunGo1A(client *kgo.Client, cfg *types.Config,
	memoryCh chan<- types.OrderMessage, stopChan <-chan struct{},
	logger *log.Logger) {

	// Phase 1: retry with a 3s timeout until the first batch of records arrives.
	for {
		select {
		case <-stopChan:
			return
		default:
		}

		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		fetches := client.PollFetches(ctx)
		cancel()

		if !fetches.Empty() {
			drainToChannel(fetches, memoryCh, stopChan, logger)
			break // first data seen; enter phase 2
		}
		if debugEnabled {
			logger.Printf("Go1A: no messages yet, will retry")
		}
	}
	
	if debugEnabled {
		logger.Printf("Go1A: The match arrived. Continuous polling start. Will check stop_chan every 100ms.")
	}
	// Phase 2: continuous polling with a short timeout so stop is responsive.
	for {
		select {
		case <-stopChan:
			return
		default:
		}

		ctx, cancel := context.WithTimeout(context.Background(), 100*time.Millisecond)
		fetches := client.PollFetches(ctx)
		cancel()

		drainToChannel(fetches, memoryCh, stopChan, logger)
	}
}

// drainToChannel iterates a poll's records, parsing and copying each one before
// handing it off to Go1B. Returns early if stop_chan closes mid-drain.
func drainToChannel(fetches kgo.Fetches, memoryCh chan<- types.OrderMessage,
	stopChan <-chan struct{}, logger *log.Logger) {

	iter := fetches.RecordIter()
	for !iter.Done() {
		record := iter.Next()

		// Parse OrdID from Tag 11 in the raw FIX bytes as a string to preserve
		// loadgen-generated ClOrdID values such as run-18-bot-95-... .
		ordID := string(fix.ParseTag(record.Value, fix.PfxClOrdID))

		// Copy raw bytes; franz-go may reuse the underlying buffer on next poll.
		raw := make([]byte, len(record.Value))
		copy(raw, record.Value)

		select {
		case memoryCh <- types.OrderMessage{OrdID: ordID, RawBytes: raw}:
		case <-stopChan:
			return
		}
		if debugEnabled {
			logger.Printf("Go1A: queued ord_id=%s bytes=%d", ordID, len(raw))
		}
	}
}
