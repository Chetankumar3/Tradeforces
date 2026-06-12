package goroutines

import (
	"bytes"
	"log"
	"sort"
	"strings"

	"github.com/iicpc/harness/internal/fix"
	"github.com/iicpc/harness/internal/types"
)

// normalize canonicalizes a FIX exec report for cross-engine comparison. It
// strips framing + engine-assigned tags {8, 9, 10, 17, 37} (which legitimately
// differ between contestant and shadow), sorts the remaining "tag=value" tokens
// alphabetically, and joins them with SOH. Two reports are equal iff their
// normalized strings are identical.
func normalize(msg []byte) string {
	excluded := map[string]bool{"8": true, "9": true, "10": true, "17": true, "37": true}
	parts := []string{}

	// Scan msg, splitting on SOH, and process each "tag=value" token.
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

// RunGo3 is the Correctness Worker. It is the ONLY goroutine that touches
// shadowPending, contestantPending, and the correctness counters. It receives
// exec reports from Go2 (contestant) and Go7 (shadow), normalizes each, and
// performs bidirectional matching. Correctness % is matched/totalShadow * 100.
func RunGo3(go3Ch <-chan []byte, shadowCh <-chan []byte,
	correctnessReqCh <-chan types.CorrectnessRequest,
	logger *log.Logger) {

	shadowPending := make(map[int]map[string]struct{})     // ClOrdID -> set of normalized shadow reports
	contestantPending := make(map[int]map[string]struct{}) // ClOrdID -> set of normalized contestant reports
	totalShadowReports := 0                                // denominator: total exec reports from shadow
	matchedReports := 0                                    // numerator: reports that matched

	for {
		select {

		case msg := <-shadowCh:
			clOrdID := fix.Atoi(fix.ParseTag(msg, fix.PfxClOrdID))
			norm := normalize(msg)
			totalShadowReports++
			// Did the contestant already send a matching report?
			if set, ok := contestantPending[clOrdID]; ok {
				if _, found := set[norm]; found {
					delete(set, norm)
					if len(set) == 0 {
						delete(contestantPending, clOrdID)
					}
					matchedReports++
					if debugEnabled {
						logger.Printf("Go3: MATCH (shadow late) ord_id=%d score=%d/%d",
							clOrdID, matchedReports, totalShadowReports)
					}
					continue
				}
			}
			// Contestant has not sent this report yet; store in shadowPending.
			if shadowPending[clOrdID] == nil {
				shadowPending[clOrdID] = make(map[string]struct{})
			}
			shadowPending[clOrdID][norm] = struct{}{}
			if debugEnabled {
				logger.Printf("Go3: shadow stored ord_id=%d total_shadow=%d",
					clOrdID, totalShadowReports)
			}

		case msg := <-go3Ch:
			clOrdID := fix.Atoi(fix.ParseTag(msg, fix.PfxClOrdID))
			norm := normalize(msg)
			// Did the shadow already send a matching report?
			if set, ok := shadowPending[clOrdID]; ok {
				if _, found := set[norm]; found {
					delete(set, norm)
					if len(set) == 0 {
						delete(shadowPending, clOrdID)
					}
					matchedReports++
					if debugEnabled {
						logger.Printf("Go3: MATCH (contestant late) ord_id=%d score=%d/%d",
							clOrdID, matchedReports, totalShadowReports)
					}
					continue
				}
			}
			// Shadow has not sent this report yet; store in contestantPending.
			if contestantPending[clOrdID] == nil {
				contestantPending[clOrdID] = make(map[string]struct{})
			}
			contestantPending[clOrdID][norm] = struct{}{}
			if debugEnabled {
				logger.Printf("Go3: contestant stored, no shadow yet ord_id=%d", clOrdID)
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
