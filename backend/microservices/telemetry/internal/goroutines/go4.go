package goroutines

import (
	"log"

	hdrhistogram "github.com/HdrHistogram/hdrhistogram-go"
	"github.com/iicpc/telemetry/internal/types"
)

type go4State struct {
	DS1Recorder      map[string]int64
	DS1Ring          [1_000_000]string
	ds1Head, ds1Tail int

	// DS2: TrdMatchID (Tag 880) -> []int64.
	//   [0]    : aggressor t_ingress (-1 = placeholder before aggressor seen)
	//   [1..N] : resting order arr_times awaiting the aggressor's t_ingress
	DS2ExecTracker map[int][]int64

	DS15Throughput map[int64]int
	ds15SecDeque   []int64

	DS16MaxThroughput int

	ACKHistogram  *hdrhistogram.Histogram
	EXECHistogram *hdrhistogram.Histogram
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

// RunGo4 only ever sees contestant data (from Go2) -- the shadow engine never
// feeds Go4; correctness against the shadow is Go3's job exclusively.
func RunGo4(ingressCh <-chan types.IngressEvent, egressCh <-chan types.EgressEvent,
	scoreReqCh <-chan types.ScoreRequest, logger *log.Logger) {

	s := newGo4State()

	for {
		select {
		case ev := <-ingressCh:
			s.handleIngress(ev.OrdID, ev.TIngress, logger)

		case ev := <-egressCh:
			s.handleEgress(ev.OrdID, ev.MatchID, ev.Aggressor, ev.ArrTime, logger)

		case req := <-scoreReqCh:
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

func (s *go4State) handleIngress(ordID string, tIngress int64, logger *log.Logger) {
	sec := tIngress / 1_000_000_000

	if s.DS15Throughput[sec] == 0 {
		s.ds15SecDeque = append(s.ds15SecDeque, sec)
	}
	s.DS15Throughput[sec]++
	if s.DS15Throughput[sec] > s.DS16MaxThroughput {
		s.DS16MaxThroughput = s.DS15Throughput[sec]
	}

	for len(s.ds15SecDeque) > 0 && s.ds15SecDeque[0] < sec-60 {
		delete(s.DS15Throughput, s.ds15SecDeque[0])
		s.ds15SecDeque = s.ds15SecDeque[1:]
	}

	if len(s.DS1Recorder) >= 1_000_000 {
		evictID := s.DS1Ring[s.ds1Head]
		s.ds1Head = (s.ds1Head + 1) % 1_000_000
		delete(s.DS1Recorder, evictID)
	}
	s.DS1Recorder[ordID] = tIngress
	s.DS1Ring[s.ds1Tail] = ordID
	s.ds1Tail = (s.ds1Tail + 1) % 1_000_000

	if debugEnabled {
		logger.Printf("Go4: ingress ord_id=%s sec=%d rps=%d",
			ordID, sec, s.DS15Throughput[sec])
	}
}

// handleEgress matches an exec report to its ingress timestamp. All reports
// belonging to one trade share a TrdMatchID (Tag 880), which is what lets us
// find every resting order an aggressor cleared.
func (s *go4State) handleEgress(ordID string, matchID int, aggressor bool, arrTime int64, logger *log.Logger) {
	tracker := s.DS2ExecTracker[matchID]

	if len(tracker) == 0 {
		if aggressor {
			if tIn, ok := s.DS1Recorder[ordID]; ok {
				s.ACKHistogram.RecordValue(arrTime - tIn)
				s.DS2ExecTracker[matchID] = append(make([]int64, 0, 2), tIn)
				delete(s.DS1Recorder, ordID)
				if debugEnabled {
					logger.Printf("Go4: ACK lat=%dns ord_id=%s match_id=%d",
						arrTime-tIn, ordID, matchID)
				}
			}
		} else {
			s.DS2ExecTracker[matchID] = append(make([]int64, 0, 2), -1, arrTime)
			if debugEnabled {
				logger.Printf("Go4: resting before aggressor match_id=%d", matchID)
			}
		}
	} else {
		if aggressor {
			if tIn, ok := s.DS1Recorder[ordID]; ok {
				s.ACKHistogram.RecordValue(arrTime - tIn)
				s.DS2ExecTracker[matchID][0] = tIn
				delete(s.DS1Recorder, ordID)
				if debugEnabled {
					logger.Printf("Go4: late ACK lat=%dns match_id=%d", arrTime-tIn, matchID)
				}
			}
		} else {
			s.DS2ExecTracker[matchID] = append(s.DS2ExecTracker[matchID], arrTime)
		}
	}

	if len(s.DS2ExecTracker[matchID]) > 0 && s.DS2ExecTracker[matchID][0] != -1 {
		for len(s.DS2ExecTracker[matchID]) > 1 {
			last := len(s.DS2ExecTracker[matchID]) - 1
			latency := s.DS2ExecTracker[matchID][last] - s.DS2ExecTracker[matchID][0]
			s.EXECHistogram.RecordValue(latency)
			s.DS2ExecTracker[matchID] = s.DS2ExecTracker[matchID][:last]
			if debugEnabled {
				logger.Printf("Go4: EXEC lat=%dns ord_id=%s match_id=%d", latency, ordID, matchID)
			}
		}
	}
}