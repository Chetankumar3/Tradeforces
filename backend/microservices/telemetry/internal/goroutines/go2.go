package goroutines

import (
	"bufio"
	"log"
	"net"
	"time"

	"github.com/iicpc/telemetry/internal/fix"
	"github.com/iicpc/telemetry/internal/types"
)

// RunGo2 is the Egress TCP Reader for the contestant engine. It reads FIX exec
// reports over a persistent connection via a bufio.Reader (Go netpoller parks
// the goroutine until data arrives), timestamps arrival immediately, parses the
// key fields, then forwards to Go4 (latency path) and Go3 (correctness path).
func RunGo2(conn net.Conn, egressCh chan<- types.EgressEvent,
	go3Ch chan<- []byte, stopChan <-chan struct{},
	logger *log.Logger) {

	br := bufio.NewReaderSize(conn, 65536) // one reader reused for all messages
	var scratch [4096]byte                 // one stack buffer reused for all messages

	for {
		n, err := fix.ReadFIXMessage(br, scratch[:])
		if err != nil {
			if debugEnabled {
				logger.Printf("Go2: read error: %v", err)
			}
			return
		}

		arrTime := time.Now().UnixNano() // IMMEDIATELY after read -- no code between read and here

		msg := scratch[:n] // slice into stack buffer; valid until next ReadFIXMessage call

		// Parse key fields - all zero-alloc slices into msg.
		ordID := string(fix.ParseTag(msg, fix.PfxClOrdID))
		execID := fix.Atoi(fix.ParseTag(msg, fix.PfxExecID))
		aggrVal := fix.ParseTag(msg, fix.PfxAggressor)
		aggressor := len(aggrVal) > 0 && aggrVal[0] == 'Y'

		// Push to the latency path FIRST to minimize the timestamp delta.
		select {
		case egressCh <- types.EgressEvent{OrdID: ordID, ExecID: execID,
			Aggressor: aggressor, ArrTime: arrTime}:
		case <-stopChan:
			return
		}

		// Copy for Go3 (one alloc per message; Go3 is not latency-critical).
		copyBuf := make([]byte, n)
		copy(copyBuf, scratch[:n])

		// Non-blocking: drop if Go3 is behind rather than stalling Go2.
		select {
		case go3Ch <- copyBuf:
		default:
			if debugEnabled {
				logger.Printf("Go2: go3 channel full, dropping ord_id=%s", ordID)
			}
		}

		if debugEnabled {
			logger.Printf("Go2: exec report ord_id=%s exec_id=%d aggressor=%v t=%d",
				ordID, execID, aggressor, arrTime)
		}
	}
}
