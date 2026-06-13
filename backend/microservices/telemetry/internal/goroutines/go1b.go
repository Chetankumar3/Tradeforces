package goroutines

import (
	"errors"
	"log"
	"net"
	"runtime"
	"sync"
	"syscall"
	"time"

	"github.com/iicpc/telemetry/internal/types"
)

// RunGo1B is the Ingress TCP Writer. It reads OrderMessages from memoryCh and
// writes their raw FIX bytes to the contestant engine via syscall.Write on a raw
// fd (non-blocking, so kernel EAGAIN is visible). Immediately after the write it
// timestamps the ingress and forwards an IngressEvent to Go4.
//
// ingressConn is passed (and held) only to keep the connection alive so the GC
// cannot finalize it and invalidate the raw fd.
func RunGo1B(fd uintptr, ingressConn net.Conn,
	memoryCh <-chan types.OrderMessage, ingressCh chan<- types.IngressEvent,
	stopChan chan struct{}, logger *log.Logger) {

	var stopOnce sync.Once
	blockageCount := 0

	// permanentStop closes stop_chan exactly once (sync.Once guarantees single close).
	permanentStop := func() {
		stopOnce.Do(func() { close(stopChan) })
	}

	for {
		var msg types.OrderMessage
		select {
		case <-stopChan:
			return
		case msg = <-memoryCh:
		}

		if err := writeAll(fd, msg.RawBytes, &blockageCount, permanentStop, logger); err != nil {
			return // stop_chan already closed inside writeAll
		}

		tIngress := time.Now().UnixNano() // IMMEDIATELY after write -- no code between write and here

		select {
		case ingressCh <- types.IngressEvent{OrdID: msg.OrdID, TIngress: tIngress}:
		case <-stopChan:
			return
		}

		if debugEnabled {
			logger.Printf("Go1B: sent ord_id=%d t=%d", msg.OrdID, tIngress)
		}
	}
}

// writeAll writes the entire buffer to the raw fd, handling partial writes and
// kernel backpressure (EAGAIN). Backpressure policy:
//   - if a single write blocks (EAGAIN) for more than 15s, stop permanently;
//   - if EAGAIN is resolved 4 or more times across the session, stop permanently.
func writeAll(fd uintptr, buf []byte,
	blockageCount *int, permanentStop func(),
	logger *log.Logger) error {

	written := 0
	blockStart := time.Time{}
	wasBlocked := false

	for written < len(buf) {
		n, err := syscall.Write(int(fd), buf[written:])
		if n > 0 {
			written += n
		}

		if err == syscall.EAGAIN || err == syscall.EWOULDBLOCK {
			if !wasBlocked {
				blockStart = time.Now()
				wasBlocked = true
				if debugEnabled {
					logger.Printf("Go1B: EAGAIN start, blockage_count=%d", *blockageCount)
				}
			}
			if time.Since(blockStart) > 15*time.Second {
				if debugEnabled {
					logger.Printf("Go1B: single blockage exceeded 15s, stopping permanently")
				}
				permanentStop()
				return errors.New("permanent stop: 15s single blockage")
			}
			runtime.Gosched() // yield; retry the same write
			continue
		}

		if err != nil {
			if debugEnabled {
				logger.Printf("Go1B: write error: %v", err)
			}
			return err
		}
		// err == nil: partial or full write succeeded; loop continues if written < len(buf).
	}

	// All bytes written. If we hit EAGAIN before completing, count one blockage event.
	if wasBlocked {
		*blockageCount++
		if debugEnabled {
			logger.Printf("Go1B: EAGAIN resolved, blockage_count=%d", *blockageCount)
		}
		if *blockageCount >= 4 {
			if debugEnabled {
				logger.Printf("Go1B: 4 blockages reached, stopping permanently")
			}
			permanentStop()
			return errors.New("permanent stop: 4 blockages")
		}
	}

	return nil
}
