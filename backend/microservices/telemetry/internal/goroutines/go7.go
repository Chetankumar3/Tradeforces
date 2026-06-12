package goroutines

import (
	"bufio"
	"log"
	"net"

	"github.com/iicpc/harness/internal/fix"
)

// RunGo7 is the Shadow Engine Reader. Structurally the same as Go2 but the
// shadow (reference) engine is trusted upstream, so it reads in plain blocking
// mode with no backpressure handling and captures no latency timestamps. It
// forwards byte copies to Go3 for correctness comparison.
func RunGo7(conn net.Conn, shadowCh chan<- []byte, logger *log.Logger) {

	br := bufio.NewReaderSize(conn, 65536)
	var scratch [4096]byte

	for {
		n, err := fix.ReadFIXMessage(br, scratch[:])
		if err != nil {
			if debugEnabled {
				logger.Printf("Go7: read error: %v", err)
			}
			return
		}

		copyBuf := make([]byte, n)
		copy(copyBuf, scratch[:n])
		shadowCh <- copyBuf // blocking send; shadow channel should not fill up

		if debugEnabled {
			ordID := fix.Atoi(fix.ParseTag(scratch[:n], fix.PfxClOrdID))
			logger.Printf("Go7: shadow exec report ord_id=%d", ordID)
		}
	}
}
