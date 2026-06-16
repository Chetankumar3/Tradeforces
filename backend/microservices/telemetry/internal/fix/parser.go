// Package fix provides zero-allocation FIX 4.2 parsing helpers used on the hot path.
package fix

import (
	"bufio"
	"bytes"
	"errors"
	"io"
)

var (
	// Tag 11 - ClOrdID. STABLE across engines: the same order is replayed to
	// both contestant and shadow with the same ClOrdID. Go3 uses this to
	// correlate which contestant report corresponds to which shadow report.
	PfxClOrdID = []byte("\x0111=")

	// Tag 880 - TrdMatchID. Engine-internal trade-grouping id: shared by an
	// aggressor's exec report and every resting order it cleared. NOT stable
	// across engines (each assigns its own). Used ONLY by Go4 to compute
	// exec_latency = T_egress(resting fill) - T_ingress(aggressor). Go3 must
	// exclude it from correctness comparison.
	PfxTrdMatchID = []byte("\x01880=")

	// Tag 1057 (custom) - aggressor-side indicator, 'Y'/'N'.
	PfxAggressor = []byte("\x011057=")
)

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

func Atoi(b []byte) int {
	n := 0
	for _, c := range b {
		n = n*10 + int(c-'0')
	}
	return n
}

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

func ReadFIXMessage(br *bufio.Reader, scratch []byte) (int, error) {
	pos := 0
	sohCount := 0
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
	bodyLen := ParseBodyLen(scratch[:pos])
	if bodyLen <= 0 {
		return 0, errors.New("fix: invalid bodyLen")
	}
	if _, err := io.ReadFull(br, scratch[pos:pos+bodyLen]); err != nil {
		return 0, err
	}
	return pos + bodyLen, nil
}