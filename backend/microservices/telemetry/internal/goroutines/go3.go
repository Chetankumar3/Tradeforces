package goroutines

import (
	"bytes"
	"log"
	"sort"
	"strings"

	"github.com/iicpc/telemetry/internal/fix"
	"github.com/iicpc/telemetry/internal/types"
)

// normalize canonicalizes a FIX exec report for cross-engine comparison. It
// strips framing + engine-assigned tags {8, 9, 10, 17, 37} (which legitimately
// differ between contestant and shadow), sorts the remaining "tag=value" tokens
// alphabetically, and joins them with SOH. Two reports are equal iff their
// normalized strings are identical.
func normalize(msg []byte) string {
	// Strip FIX framing plus identity fields that are not stable across
	// contestant/shadow runs (e.g. ClOrdID / OrigClOrdID / engine-generated IDs).
	excluded := map[string]bool{
		"8":  true,
		"9":  true,
		"10": true,
		"11": true,
		"17": true,
		"37": true,
		"41": true,
	}
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

	shadowPending := make(map[string]struct{})     // normalized shadow report fingerprints
	contestantPending := make(map[string]struct{}) // normalized contestant report fingerprints
	totalShadowReports := 0                        // denominator: total exec reports from shadow
	matchedReports := 0                            // numerator: reports that matched

	for {
		select {

		case msg := <-shadowCh:
			clOrdID := string(fix.ParseTag(msg, fix.PfxClOrdID))
			norm := normalize(msg)
			totalShadowReports++
			// Did the contestant already send a matching normalized report?
			if _, found := contestantPending[norm]; found {
				delete(contestantPending, norm)
				matchedReports++
				if debugEnabled {
					logger.Printf("Go3: MATCH (shadow late) ord_id=%s score=%d/%d",
						clOrdID, matchedReports, totalShadowReports)
				}
				continue
			}
			// Contestant has not sent this report yet; store in shadowPending.
			shadowPending[norm] = struct{}{}
			if debugEnabled {
				logger.Printf("Go3: shadow stored ord_id=%s total_shadow=%d",
					clOrdID, totalShadowReports)
			}

		case msg := <-go3Ch:
			clOrdID := string(fix.ParseTag(msg, fix.PfxClOrdID))
			norm := normalize(msg)
			// Did the shadow already send a matching normalized report?
			if _, found := shadowPending[norm]; found {
				delete(shadowPending, norm)
				matchedReports++
				if debugEnabled {
					logger.Printf("Go3: MATCH (contestant late) ord_id=%s score=%d/%d",
						clOrdID, matchedReports, totalShadowReports)
				}
				continue
			}
			// Shadow has not sent this report yet; store in contestantPending.
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
