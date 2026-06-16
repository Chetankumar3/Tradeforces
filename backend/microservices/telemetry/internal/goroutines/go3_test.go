package goroutines

import (
	"strings"
	"testing"
)

func makeExecReport(clOrdID, orderID, matchID string) []byte {
	fields := []string{
		"8=FIX.4.2",
		"9=100",
		"35=8",
		"11=" + clOrdID,
		"37=" + orderID,
		"880=" + matchID,
		"39=0",
		"150=0",
		"151=0",
		"10=000",
	}
	return []byte(strings.Join(fields, "\x01") + "\x01")
}

// Same ClOrdID (same order, replayed to both engines), different engine-
// assigned OrderID/TrdMatchID -> must normalize identically.
func TestNormalizeIgnoresEngineAssignedTags(t *testing.T) {
	msgA := makeExecReport("run-94-bot-59-1781506691635775500", "ORDER-94", "777")
	msgB := makeExecReport("run-94-bot-59-1781506691635775500", "ORDER-94-SHADOW", "999")

	if got := normalize(msgA); got != normalize(msgB) {
		t.Fatalf("normalize() should ignore engine-assigned tags (37, 880), got different forms:\nA=%q\nB=%q", got, normalize(msgB))
	}
}

// Different ClOrdID is a genuinely different order -- must NOT be treated as
// the same report even if every other field matches.
func TestNormalizeDistinguishesByClOrdID(t *testing.T) {
	msgA := makeExecReport("run-94-bot-59-1781506691635775500", "ORDER-94", "777")
	msgB := makeExecReport("run-20-bot-59-1781506691635775500", "ORDER-20", "777")

	if got := normalize(msgA); got == normalize(msgB) {
		t.Fatalf("normalize() should distinguish by ClOrdID (tag 11), got identical form: %q", got)
	}
}