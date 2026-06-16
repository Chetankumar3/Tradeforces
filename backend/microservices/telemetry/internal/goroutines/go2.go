package goroutines

import (
	"bufio"
	"log"
	"net"
	"time"

	"github.com/iicpc/telemetry/internal/fix"
	"github.com/iicpc/telemetry/internal/types"
)

func RunGo2(conn net.Conn, egressCh chan<- types.EgressEvent,
	go3Ch chan<- []byte, stopChan <-chan struct{},
	logger *log.Logger) {

	br := bufio.NewReaderSize(conn, 65536)
	var scratch [4096]byte

	for {
		n, err := fix.ReadFIXMessage(br, scratch[:])
		if err != nil {
			if debugEnabled {
				logger.Printf("Go2: read error: %v", err)
			}
			return
		}

		arrTime := time.Now().UnixNano()

		msg := scratch[:n]

		ordID := string(fix.ParseTag(msg, fix.PfxClOrdID))
		matchID := fix.Atoi(fix.ParseTag(msg, fix.PfxTrdMatchID))
		aggrVal := fix.ParseTag(msg, fix.PfxAggressor)
		aggressor := len(aggrVal) > 0 && aggrVal[0] == 'Y'

		select {
		case egressCh <- types.EgressEvent{OrdID: ordID, MatchID: matchID,
			Aggressor: aggressor, ArrTime: arrTime}:
		case <-stopChan:
			return
		}

		copyBuf := make([]byte, n)
		copy(copyBuf, scratch[:n])

		select {
		case go3Ch <- copyBuf:
		default:
			if debugEnabled {
				logger.Printf("Go2: go3 channel full, dropping ord_id=%s", ordID)
			}
		}

		if debugEnabled {
			logger.Printf("Go2: exec report ord_id=%s match_id=%d aggressor=%v t=%d",
				ordID, matchID, aggressor, arrTime)
		}
	}
}