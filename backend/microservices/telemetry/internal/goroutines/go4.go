package goroutines

import (
	"log"

	hdrhistogram "github.com/HdrHistogram/hdrhistogram-go"
	"github.com/iicpc/telemetry/internal/types"
)

// go4State bundles every data structure Go4 owns. Wrapping them in a struct with
// methods keeps ownership strictly inside the Go4 goroutine — no other goroutine
// can reach these fields, so no locks are ever required.
type go4State struct {
	// DS1: OrdID -> t_ingress, with FIFO ring-buffer eviction at 1,000,000 entries.
	DS1Recorder      map[string]int64
	DS1Ring          [1_000_000]string
	ds1Head, ds1Tail int

	// DS2: ExecID -> []int64.
	//   [0]    : aggressor t_ingress (-1 = placeholder before aggressor seen)
	//   [1..N] : resting order arr_times awaiting the aggressor's t_ingress
	DS2ExecTracker map[int][]int64

	// DS15: Unix second -> request count, with a 60s rolling window deque.
	DS15Throughput map[int64]int
	ds15SecDeque   []int64

	// DS16: peak RPS seen in any single second.
	DS16MaxThroughput int

	ACKHistogram  *hdrhistogram.Histogram // aggressor ack latency; 1ns..10s, 3 sig figs
	EXECHistogram *hdrhistogram.Histogram // resting fill latency; same range
}

func newGo4State() *go4State {
	return &go4State{
		DS1Recorder:    make(map[string]int64, 1_000_000),
		DS2ExecTracker: make(map[int][]int64, 100_000),
		DS15Throughput: make(map[int64]int),
		ds15SecDeque:   make([]int64, 0, 120),
		ACKHistogram:   hdrhistogram.New(1, 10_000_000_000, 3),
		EXECHistogram:  hdrhistogram.New(1, 10_000_000_000, 3),
	}
}

// RunGo4 is the State and Latency Manager: the single source of truth for all
// latency and throughput state. It is the ONLY goroutine that touches DS1, DS2,
// DS15, DS16, and the two histograms, so it needs no locks. It serves score
// snapshots synchronously over the embedded response channel.
func RunGo4(ingressCh <-chan types.IngressEvent, egressCh <-chan types.EgressEvent,
	scoreReqCh <-chan types.ScoreRequest, logger *log.Logger) {

	s := newGo4State()

	for {
		select {
		case ev := <-ingressCh:
			s.handleIngress(ev.OrdID, ev.TIngress, logger)

		case ev := <-egressCh:
			s.handleEgress(ev.OrdID, ev.ExecID, ev.Aggressor, ev.ArrTime, logger)

		case req := <-scoreReqCh:
			// Respond synchronously; Go6 is blocking on req.RespCh.
			req.RespCh <- types.ScoreSnapshot{
				ACK_P50:       s.ACKHistogram.ValueAtPercentile(50),
				ACK_P90:       s.ACKHistogram.ValueAtPercentile(90),
				ACK_P99:       s.ACKHistogram.ValueAtPercentile(99),
				EXEC_P50:      s.EXECHistogram.ValueAtPercentile(50),
				EXEC_P90:      s.EXECHistogram.ValueAtPercentile(90),
				EXEC_P99:      s.EXECHistogram.ValueAtPercentile(99),
				MaxThroughput: s.DS16MaxThroughput,
			}
			if debugEnabled {
				logger.Printf("Go4: served score snapshot p99_ack=%dns peak_rps=%d",
					s.ACKHistogram.ValueAtPercentile(99), s.DS16MaxThroughput)
			}
		}
	}
}

// handleIngress records an order's send time, updates the per-second throughput
// counters, and inserts into DS1 with FIFO ring-buffer eviction.
func (s *go4State) handleIngress(ordID string, tIngress int64, logger *log.Logger) {
	sec := tIngress / 1_000_000_000

	// Update the throughput counter for this second.
	if s.DS15Throughput[sec] == 0 {
		s.ds15SecDeque = append(s.ds15SecDeque, sec)
	}
	s.DS15Throughput[sec]++
	if s.DS15Throughput[sec] > s.DS16MaxThroughput {
		s.DS16MaxThroughput = s.DS15Throughput[sec]
	}

	// Drop DS15 entries older than 60s from the rolling window.
	for len(s.ds15SecDeque) > 0 && s.ds15SecDeque[0] < sec-60 {
		delete(s.DS15Throughput, s.ds15SecDeque[0])
		s.ds15SecDeque = s.ds15SecDeque[1:]
	}

	// Insert into DS1 with FIFO ring-buffer eviction at 1,000,000 entries.
	if len(s.DS1Recorder) >= 1_000_000 {
		evictID := s.DS1Ring[s.ds1Head]
		s.ds1Head = (s.ds1Head + 1) % 1_000_000
		delete(s.DS1Recorder, evictID) // silent drop of the oldest order
	}
	s.DS1Recorder[ordID] = tIngress
	s.DS1Ring[s.ds1Tail] = ordID
	s.ds1Tail = (s.ds1Tail + 1) % 1_000_000

	if debugEnabled {
		logger.Printf("Go4: ingress ord_id=%s sec=%d rps=%d",
			ordID, sec, s.DS15Throughput[sec])
	}
}

// handleEgress matches an exec report to its ingress timestamp and records the
// appropriate latency. Aggressor reports yield ACK latency (arr - t_ingress);
// resting reports yield EXEC latency once the aggressor's t_ingress is known.
// All reports of one trade share an ExecID, which links them inside DS2.
func (s *go4State) handleEgress(ordID string, execID int, aggressor bool, arrTime int64, logger *log.Logger) {
	tracker := s.DS2ExecTracker[execID] // nil if first time seeing this exec_id

	if len(tracker) == 0 {
		if aggressor {
			if tIn, ok := s.DS1Recorder[ordID]; ok {
				s.ACKHistogram.RecordValue(arrTime - tIn)
				s.DS2ExecTracker[execID] = append(make([]int64, 0, 2), tIn)
				delete(s.DS1Recorder, ordID)
				if debugEnabled {
					logger.Printf("Go4: ACK lat=%dns ord_id=%s exec_id=%d",
						arrTime-tIn, ordID, execID)
				}
			}
		} else {
			// Resting order report arrived before the aggressor report for this trade.
			s.DS2ExecTracker[execID] = append(make([]int64, 0, 2), -1, arrTime)
			if debugEnabled {
				logger.Printf("Go4: resting before aggressor exec_id=%d", execID)
			}
		}
	} else {
		if aggressor {
			if tIn, ok := s.DS1Recorder[ordID]; ok {
				s.ACKHistogram.RecordValue(arrTime - tIn)
				s.DS2ExecTracker[execID][0] = tIn // replace -1 placeholder with real t_ingress
				delete(s.DS1Recorder, ordID)
				if debugEnabled {
					logger.Printf("Go4: late ACK lat=%dns exec_id=%d", arrTime-tIn, execID)
				}
			}
		} else {
			s.DS2ExecTracker[execID] = append(s.DS2ExecTracker[execID], arrTime)
		}
	}

	// Drain buffered resting-order timestamps now that the aggressor t_ingress is known.
	if len(s.DS2ExecTracker[execID]) > 0 && s.DS2ExecTracker[execID][0] != -1 {
		for len(s.DS2ExecTracker[execID]) > 1 {
			last := len(s.DS2ExecTracker[execID]) - 1
			latency := s.DS2ExecTracker[execID][last] - s.DS2ExecTracker[execID][0]
			s.EXECHistogram.RecordValue(latency)
			s.DS2ExecTracker[execID] = s.DS2ExecTracker[execID][:last] // pop back
			if debugEnabled {
				logger.Printf("Go4: EXEC lat=%dns ord_id=%s exec_id=%d", latency, ordID, execID)
			}
		}
		// DS2ExecTracker[execID] now holds [t_ingress] only; kept for future resting reports.
	}
}
