// Package fix provides zero-allocation FIX 4.2 parsing helpers used on the hot path.
package fix

import (
	"bufio"
	"bytes"
	"errors"
	"io"
)

// Package-level tag prefix byte slices. Never modified.
// The leading \x01 (SOH) enforces field-boundary matching, preventing e.g.
// "111=" from matching a search for "11=". All tags in egress messages are
// preceded by SOH since they appear after earlier fields.
var (
	PfxClOrdID   = []byte("\x0111=")
	PfxExecID    = []byte("\x01880=")
	PfxAggressor = []byte("\x011057=")
)

// ParseTag returns the value bytes for a tag as a slice into msg. ZERO allocs.
func ParseTag(msg []byte, soHPrefixedTagEq []byte) []byte {
	idx := bytes.Index(msg, soHPrefixedTagEq)
	if idx == -1 {
		return nil
	}
	start := idx + len(soHPrefixedTagEq)
	end := bytes.IndexByte(msg[start:], 0x01)
	if end == -1 {
		return nil
	}
	return msg[start : start+end]
}

// Atoi converts ASCII decimal bytes to int. ZERO allocs.
func Atoi(b []byte) int {
	n := 0
	for _, c := range b {
		n = n*10 + int(c-'0')
	}
	return n
}

// ParseBodyLen finds "9=" in the header bytes and returns the integer value.
func ParseBodyLen(header []byte) int {
	prefix := []byte("9=")
	idx := bytes.Index(header, prefix)
	if idx == -1 {
		return 0
	}
	start := idx + 2
	end := bytes.IndexByte(header[start:], 0x01)
	if end == -1 {
		return 0
	}
	return Atoi(header[start : start+end])
}

// ReadFIXMessage reads one complete FIX message into scratch. ZERO allocs.
// scratch is caller-owned and must be >= 4096 bytes. Returns bytes written.
//
// Phase 1: read byte-by-byte until two SOHs are seen.
//          After this, scratch[0:pos] = "8=FIX.4.2\x01 9=NNN\x01".
// Phase 2: parse bodyLen from scratch, then io.ReadFull exactly bodyLen more
//          bytes. Those bytes contain tag 35 through "10=XXX\x01".
func ReadFIXMessage(br *bufio.Reader, scratch []byte) (int, error) {
	pos := 0
	sohCount := 0
	// Phase 1: scan the header until both SOHs (after 8=... and after 9=...) seen.
	for sohCount < 2 {
		b, err := br.ReadByte()
		if err != nil {
			return 0, err
		}
		scratch[pos] = b
		pos++
		if b == 0x01 {
			sohCount++
		}
	}
	// Phase 2: read exactly bodyLen bytes (tag 35 through the trailing checksum SOH).
	bodyLen := ParseBodyLen(scratch[:pos])
	if bodyLen <= 0 {
		return 0, errors.New("fix: invalid bodyLen")
	}
	if _, err := io.ReadFull(br, scratch[pos:pos+bodyLen]); err != nil {
		return 0, err
	}
	return pos + bodyLen, nil
}
