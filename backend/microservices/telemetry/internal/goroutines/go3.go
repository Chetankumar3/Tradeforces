package goroutines

import (
	"bytes"
	"log"
	"sort"
	"strings"

	"github.com/iicpc/telemetry/internal/fix"
	"github.com/iicpc/telemetry/internal/types"
)

// normalize canonicalizes a FIX exec report for cross-engine comparison.
//
// Tag 11 (ClOrdID) is KEPT: it's stable across engines (same order replayed to
// both contestant and shadow), so it's the implicit join key -- two reports
// only match if they belong to the same order AND have matching content.
//
// Everything assigned independently per engine (framing, per-report ids, the
// trade-grouping id, sequence numbers, comp ids, wall-clock timestamps) is
// stripped, since those legitimately differ between contestant and shadow
// even for an identical logical fill.
func normalize(msg []byte) string {
	excluded := map[string]bool{
		"8":   true, // BeginString (framing)
		"9":   true, // BodyLength (framing)
		"10":  true, // CheckSum (framing)
		"17":  true, // ExecID - unique per report, per engine
		"34":  true, // MsgSeqNum - per-connection sequence
		"37":  true, // OrderID - engine-assigned
		"41":  true, // OrigClOrdID - engine/session bookkeeping
		"49":  true, // SenderCompID - differs per engine/connection
		"52":  true, // SendingTime - wall clock, differs per engine
		"56":  true, // TargetCompID - differs per engine/connection
		"60":  true, // TransactTime - wall clock, differs per engine
		"880": true, // TrdMatchID - Go4's internal grouping id, not a correctness field
	}
	parts := []string{}

	for _, token := range bytes.Split(msg, []byte{0x01}) {
		if len(token) == 0 {
			continue
		}
		eqIdx := bytes.IndexByte(token, '=')
		if eqIdx == -1 {
			continue
		}
		if excluded[string(token[:eqIdx])] {
			continue
		}
		parts = append(parts, string(token))
	}
	sort.Strings(parts)
	return strings.Join(parts, "\x01")
}

// RunGo3 is the Correctness Worker, the only goroutine touching shadowPending,
// contestantPending, and the counters. It receives reports from Go2
// (contestant) and Go7 (shadow), normalizes each -- now keyed implicitly by
// ClOrdID via normalize() -- and performs bidirectional matching.
func RunGo3(go3Ch <-chan []byte, shadowCh <-chan []byte,
	correctnessReqCh <-chan types.CorrectnessRequest,
	logger *log.Logger) {

	shadowPending := make(map[string]struct{})
	contestantPending := make(map[string]struct{})
	totalShadowReports := 0
	matchedReports := 0

	for {
		select {

		case msg := <-shadowCh:
			clOrdID := string(fix.ParseTag(msg, fix.PfxClOrdID))
			norm := normalize(msg)
			totalShadowReports++
			if _, found := contestantPending[norm]; found {
				delete(contestantPending, norm)
				matchedReports++
				if debugEnabled {
					logger.Printf("Go3: MATCH (shadow late) ord_id=%s score=%d/%d",
						clOrdID, matchedReports, totalShadowReports)
				}
				continue
			}
			shadowPending[norm] = struct{}{}
			if debugEnabled {
				logger.Printf("Go3: shadow stored ord_id=%s total_shadow=%d",
					clOrdID, totalShadowReports)
			}

		case msg := <-go3Ch:
			clOrdID := string(fix.ParseTag(msg, fix.PfxClOrdID))
			norm := normalize(msg)
			if _, found := shadowPending[norm]; found {
				delete(shadowPending, norm)
				matchedReports++
				if debugEnabled {
					logger.Printf("Go3: MATCH (contestant late) ord_id=%s score=%d/%d",
						clOrdID, matchedReports, totalShadowReports)
				}
				continue
			}
			contestantPending[norm] = struct{}{}
			if debugEnabled {
				logger.Printf("Go3: contestant stored, no shadow yet ord_id=%s", clOrdID)
			}

		case req := <-correctnessReqCh:
			pct := 0.0
			if totalShadowReports > 0 {
				pct = float64(matchedReports) / float64(totalShadowReports) * 100.0
			}
			req.RespCh <- pct
			if debugEnabled {
				logger.Printf("Go3: correctness=%.2f%% matched=%d total=%d",
					pct, matchedReports, totalShadowReports)
			}
		}
	}
}