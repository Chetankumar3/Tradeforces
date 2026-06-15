package goroutines

import (
	"strings"
	"testing"
)

func makeExecReport(clOrdID string, orderID string) []byte {
	fields := []string{
		"8=FIX.4.2",
		"9=100",
		"35=8",
		"11=" + clOrdID,
		"37=" + orderID,
		"39=0",
		"150=0",
		"151=0",
		"10=000",
	}
	return []byte(strings.Join(fields, "\x01") + "\x01")
}

func TestNormalizeIgnoresOrderIdentityTags(t *testing.T) {
	msgA := makeExecReport("run-94-bot-59-1781506691635775500", "ORDER-94")
	msgB := makeExecReport("run-20-bot-59-1781506691635775500", "ORDER-20")

	if got := normalize(msgA); got != normalize(msgB) {
		t.Fatalf("normalize() should ignore order identity tags, got different canonical forms:\nA=%q\nB=%q", got, normalize(msgB))
	}
}
