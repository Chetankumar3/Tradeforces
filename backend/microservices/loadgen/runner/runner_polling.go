package main

import (
	"bytes"
	"context"
	"crypto/tls"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"math"
	"math/rand"
	"net/http"
	"net/url"
	"os"
	"os/signal"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	kafka "github.com/segmentio/kafka-go"
	"github.com/segmentio/kafka-go/sasl/plain"
	"github.com/shirou/gopsutil/v3/cpu"
)

type config struct {
	RunID         string
	RunnerID      string
	ControllerURL string

	// NEW: Redpanda routing
	SubmissionID     string
	TopicName        string
	RedpandaBrokers  []string
	RedpandaUsername string
	RedpandaPassword string

	BotsPerPod        int
	RequestTimeout    time.Duration
	StartPollInterval time.Duration
	Symbols           []string
	LimitWeight       int
	MarketWeight      int
	CancelWeight      int
	Phases            []phase
}
type phase struct {
	Name            string  `json:"name"`
	DurationSeconds int     `json:"duration_seconds"`
	ActiveBots      int     `json:"active_bots"`
	RPS             float64 `json:"rps"`
}

type runStats struct {
	sent    atomic.Int64
	success atomic.Int64
	failed  atomic.Int64
	dropped atomic.Int64
}

type tickGate struct {
	mu          sync.RWMutex
	generation  int64
	phaseActive bool
	runDone     bool
	activeBots  int
	phaseStart  time.Time
	phaseEnd    time.Time
	perBotRPS   float64
	bucket      atomic.Int64
	bucketCap   int64
}
type openOrder struct {
	OrderID  string
	Side     string
	Symbol   string
	Price    float64
	Quantity int
}

type requestPlan struct {
	orderID       string
	orderType     string
	side          string
	symbol        string
	price         float64
	quantity      int
	origClOrdID   string
	cancelOrderID string
}

type tickAssignment struct {
	generation int64
	requests   int64
	stopping   bool
}

type phaseSnapshot struct {
	generation  int64
	phaseActive bool
	runDone     bool
	activeBots  int
	phaseStart  time.Time
	phaseEnd    time.Time
	perBotRPS   float64
}

type readyRequest struct {
	RunID    string `json:"run_id"`
	RunnerID string `json:"runner_id"`
}

type startResponse struct {
	Ready   bool   `json:"ready"`
	StartAt string `json:"start_at"`
	Status  string `json:"status"`
}

type resultRequest struct {
	RunID    string `json:"run_id"`
	RunnerID string `json:"runner_id"`
	Sent     int64  `json:"sent"`
	Success  int64  `json:"success"`
	Failed   int64  `json:"failed"`
	Dropped  int64  `json:"dropped"`
}

type bot struct {
	id         int
	cfg        config
	writer     *kafka.Writer // NEW
	gate       *tickGate
	stats      *runStats
	rand       *rand.Rand
	openOrders []openOrder
	mu         sync.Mutex
}

func main() {
	cfg, err := loadConfig()
	log.Printf("Runner")
	if err != nil {
		log.Fatalf("load config: %v", err)
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	// In Kubernetes mode the runner waits for the controller's START_AT before
	// generating traffic, so all runner pods begin the phase schedule together.
	if cfg.ControllerURL != "" {
		startAt, err := waitForControllerStart(ctx, cfg)
		if err != nil {
			log.Fatalf("coordinate start with controller: %v", err)
		}
		log.Printf("run_id=%s runner_id=%s waiting_until_start_at=%s", cfg.RunID, cfg.RunnerID, startAt.Format(time.RFC3339Nano))
		if !sleepUntil(ctx, startAt) {
			log.Fatalf("stopped before start_at")
		}
	}

	if err := run(ctx, cfg); err != nil {
		log.Fatalf("run bot runner: %v", err)
	}
}

func newRedpandaWriter(cfg config) (*kafka.Writer, error) {
	if len(cfg.RedpandaBrokers) == 0 {
		return nil, errors.New("REDPANDA_BROKERS is required")
	}
	if cfg.TopicName == "" {
		return nil, errors.New("TOPIC_NAME is required")
	}

	w := &kafka.Writer{
		Addr:         kafka.TCP(cfg.RedpandaBrokers...),
		Topic:        cfg.TopicName,
		Balancer:     &kafka.Hash{}, // same submission_id keeps the same partition
		RequiredAcks: kafka.RequireOne,
		BatchSize:    100,
		BatchTimeout: 5 * time.Millisecond,
		Compression:  kafka.Snappy,
	}

	// If your Redpanda Cloud cluster needs TLS/SASL, wire it here.
	if cfg.RedpandaUsername != "" {
		w.Transport = &kafka.Transport{
			TLS: &tls.Config{},
			SASL: plain.Mechanism{
				Username: cfg.RedpandaUsername,
				Password: cfg.RedpandaPassword,
			},
		}
	}

	return w, nil
}

// run starts one bot-runner process. In Kubernetes, each bot-runner pod runs
// this same process and receives its own share of the benchmark profile.
func run(ctx context.Context, cfg config) error {
	writer, err := newRedpandaWriter(cfg)
	if err != nil {
		return fmt.Errorf("create redpanda writer: %w", err)
	}
	defer writer.Close()

	stats := &runStats{}

	// metrics goroutine can stay the same

	runCtx, cancel := context.WithCancel(ctx)
	defer cancel()

	gate := newTickGate()
	go func() {
		<-runCtx.Done()
		gate.stop()
	}()

	var wg sync.WaitGroup
	for i := 0; i < cfg.BotsPerPod; i++ {
		b := &bot{
			id:     i,
			cfg:    cfg,
			writer: writer,
			gate:   gate,
			stats:  stats,
			rand:   rand.New(rand.NewSource(time.Now().UnixNano() + int64(i))),
		}

		wg.Add(1)
		go func(botInstance *bot) {
			defer wg.Done()
			botInstance.run(runCtx, gate)
		}(b)
	}

	log.Printf(
		"run_id=%s submission_id=%s topic=%s bots_per_pod=%d phases=%d",
		cfg.RunID, cfg.SubmissionID, cfg.TopicName, cfg.BotsPerPod, len(cfg.Phases),
	)

	if err := runPhases(runCtx, cfg, gate, stats); err != nil {
		gate.stop()
		wg.Wait()
		return err
	}

	gate.stop()
	wg.Wait()
	cancel()

	log.Printf(
		"run_id=%s runner_id=%s completed sent=%d success=%d failed=%d dropped=%d",
		cfg.RunID, cfg.RunnerID,
		stats.sent.Load(),
		stats.success.Load(),
		stats.failed.Load(),
		stats.dropped.Load(),
	)

	if cfg.ControllerURL != "" {
		if err := postResults(cfg, stats); err != nil {
			log.Printf("post results failed run_id=%s runner_id=%s error=%v", cfg.RunID, cfg.RunnerID, err)
		}
	}
	return nil
}
func waitForControllerStart(ctx context.Context, cfg config) (time.Time, error) {
	if err := postReady(ctx, cfg); err != nil {
		return time.Time{}, err
	}

	ticker := time.NewTicker(cfg.StartPollInterval)
	defer ticker.Stop()

	for {
		startAt, ok, err := getStart(ctx, cfg)
		if err != nil {
			return time.Time{}, err
		}
		if ok {
			return startAt, nil
		}

		select {
		case <-ctx.Done():
			return time.Time{}, ctx.Err()
		case <-ticker.C:
		}
	}
}

func postReady(ctx context.Context, cfg config) error {
	payload := readyRequest{
		RunID:    cfg.RunID,
		RunnerID: cfg.RunnerID,
	}
	return postJSON(ctx, cfg.ControllerURL+"/ready", payload, nil)
}

func getStart(ctx context.Context, cfg config) (time.Time, bool, error) {
	url := fmt.Sprintf("%s/start?run_id=%s&runner_id=%s",
		strings.TrimRight(cfg.ControllerURL, "/"),
		url.QueryEscape(cfg.RunID),
		url.QueryEscape(cfg.RunnerID),
	)

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return time.Time{}, false, err
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return time.Time{}, false, err
	}
	defer resp.Body.Close()

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return time.Time{}, false, fmt.Errorf("controller start status=%d", resp.StatusCode)
	}

	var body startResponse
	if err := json.NewDecoder(resp.Body).Decode(&body); err != nil {
		return time.Time{}, false, err
	}
	if !body.Ready {
		return time.Time{}, false, nil
	}

	startAt, err := time.Parse(time.RFC3339Nano, body.StartAt)
	if err != nil {
		return time.Time{}, false, err
	}
	return startAt, true, nil
}

func postResults(cfg config, stats *runStats) error {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	payload := resultRequest{
		RunID:    cfg.RunID,
		RunnerID: cfg.RunnerID,
		Sent:     stats.sent.Load(),
		Success:  stats.success.Load(),
		Failed:   stats.failed.Load(),
		Dropped:  stats.dropped.Load(),
	}

	return postJSON(ctx, cfg.ControllerURL+"/results", payload, nil)
}

func postJSON(ctx context.Context, endpoint string, payload any, output any) error {
	body, err := json.Marshal(payload)
	if err != nil {
		return err
	}

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, endpoint, bytes.NewReader(body))
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", "application/json")

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	_, _ = io.Copy(io.Discard, resp.Body)
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("post %s status=%d", endpoint, resp.StatusCode)
	}
	if output != nil {
		return json.NewDecoder(resp.Body).Decode(output)
	}
	return nil
}

func sleepUntil(ctx context.Context, startAt time.Time) bool {
	delay := time.Until(startAt)
	if delay <= 0 {
		return true
	}

	timer := time.NewTimer(delay)
	defer timer.Stop()

	select {
	case <-ctx.Done():
		return false
	case <-timer.C:
		return true
	}
}

func newTickGate() *tickGate {
	return &tickGate{}
}
func (g *tickGate) remainingTokens() int64 {
	return g.bucket.Load()
}

// startPhase records the active phase, resets the token bucket, and bumps the
// generation so every bot resets its per-phase counters.
func (g *tickGate) startPhase(p phase, activeBots int, bucketCap int64) {
	g.mu.Lock()
	defer g.mu.Unlock()

	g.generation++
	g.phaseActive = true
	g.runDone = false
	g.activeBots = activeBots
	g.phaseStart = time.Now()
	g.phaseEnd = g.phaseStart.Add(time.Duration(p.DurationSeconds) * time.Second)
	g.perBotRPS = 0
	if activeBots > 0 {
		g.perBotRPS = p.RPS / float64(activeBots)
	}
	g.bucket.Store(0)
	if bucketCap <= 0 {
		bucketCap = 1
	}
	g.bucketCap = bucketCap
}

// publishTick adds fresh tokens into the current phase bucket.
// Bots poll the bucket themselves, so this does not wake any goroutine.
func (g *tickGate) publishTick(activeBots int, requests int64) {
	if requests <= 0 {
		return
	}

	g.mu.RLock()
	phaseActive := g.phaseActive
	runDone := g.runDone
	currentBots := g.activeBots
	bucketCap := g.bucketCap
	g.mu.RUnlock()

	if runDone || !phaseActive || activeBots != currentBots || activeBots <= 0 {
		return
	}
	if bucketCap <= 0 {
		bucketCap = requests
	}

	for {
		current := g.bucket.Load()
		next := current + requests
		if next > bucketCap {
			next = bucketCap
		}
		if g.bucket.CompareAndSwap(current, next) {
			return
		}
	}
}

// stopPhase closes the current phase and discards any unused tokens.
func (g *tickGate) stopPhase() {
	g.mu.Lock()
	defer g.mu.Unlock()

	g.phaseActive = false
	g.bucket.Store(0)
	g.bucketCap = 0
}

// stop marks the entire run as finished and clears the bucket.
func (g *tickGate) stop() {
	g.mu.Lock()
	defer g.mu.Unlock()

	g.generation++
	g.runDone = true
	g.phaseActive = false
	g.bucket.Store(0)
	g.bucketCap = 0
}

// snapshot returns a read-only view of the current phase state for polling bots.
func (g *tickGate) snapshot() phaseSnapshot {
	g.mu.RLock()
	defer g.mu.RUnlock()

	return phaseSnapshot{
		generation:  g.generation,
		phaseActive: g.phaseActive,
		runDone:     g.runDone,
		activeBots:  g.activeBots,
		phaseStart:  g.phaseStart,
		phaseEnd:    g.phaseEnd,
		perBotRPS:   g.perBotRPS,
	}
}

// tryTakeToken removes one token from the current phase bucket if available.
func (g *tickGate) tryTakeToken() bool {
	for {
		current := g.bucket.Load()
		if current <= 0 {
			return false
		}
		if g.bucket.CompareAndSwap(current, current-1) {
			return true
		}
	}
}

// run is one preallocated virtual bot. It polls the current phase quota, launches
// at most one request per poll, and then yields so it does not busy-spin.
func (b *bot) run(ctx context.Context, gate *tickGate) {
	var seenGeneration int64
	var sentThisPhase int64

	for {
		snap := gate.snapshot()

		if snap.runDone {
			return
		}

		if snap.generation != seenGeneration {
			seenGeneration = snap.generation
			sentThisPhase = 0
		}

		if !snap.phaseActive || b.id >= snap.activeBots {
			if ctx.Err() != nil {
				return
			}
			runtime.Gosched()
			continue
		}

		elapsed := time.Since(snap.phaseStart).Seconds()
		expected := int64(math.Floor(elapsed * snap.perBotRPS))
		if sentThisPhase >= expected {
			runtime.Gosched()
			continue
		}

		if !gate.tryTakeToken() {
			runtime.Gosched()
			continue
		}

		sentThisPhase++

		if ctx.Err() != nil {
			return
		}

		// Launching asynchronously means this bot does not wait for the
		// contestant response before creating the next planned request.
		b.launchAction(ctx)
	}
}

// runPhases executes the configured ramp. Each phase has one local scheduler
// clock per pod, and the bots poll their own quotas instead of being woken up.
func runPhases(ctx context.Context, cfg config, gate *tickGate, stats *runStats) error {
	for _, p := range cfg.Phases {
		active := min(p.ActiveBots, cfg.BotsPerPod)
		tick := schedulerTick(p.RPS)
		bucketCap := int64(math.Ceil(p.RPS * tick.Seconds()))
		bucketCap *= 2
		if bucketCap <= 0 {
			bucketCap = 1
		}

		beforeSent := stats.sent.Load()
		beforeSuccess := stats.success.Load()
		beforeFailed := stats.failed.Load()
		beforeDropped := stats.dropped.Load()

		log.Printf(
			"PHASE START name=%s duration=%ds active_bots=%d target_rps=%.2f tick=%s",
			p.Name,
			p.DurationSeconds,
			active,
			p.RPS,
			tick,
		)

		gate.startPhase(p, active, bucketCap)

		start := time.Now()
		minted := runPhase(ctx, p, active, tick, gate, stats)

		remaining := gate.remainingTokens()

		gate.stopPhase()
		duration := time.Since(start).Seconds()

		phaseSent := stats.sent.Load() - beforeSent
		phaseSuccess := stats.success.Load() - beforeSuccess
		phaseFailed := stats.failed.Load() - beforeFailed
		phaseDropped := stats.dropped.Load() - beforeDropped
		achievedRPS := float64(phaseSent) / maxFloat(duration, 0.001)

		log.Printf(
			"PHASE END name=%s minted=%d remaining=%d consumed=%d sent=%d success=%d failed=%d dropped=%d achieved_rps=%.2f cpu=%.2f%%",
			p.Name,
			minted,
			remaining,
			minted-remaining,
			phaseSent,
			phaseSuccess,
			phaseFailed,
			phaseDropped,
			achievedRPS,
			getCPUUsage(),
		)

		select {
		case <-ctx.Done():
			return ctx.Err()
		default:
		}
	}

	return nil
}

// runPhase refills the phase bucket every scheduler tick and lets bots poll for
// their own quotas independently.
func runPhase(
	ctx context.Context,
	p phase,
	activeBots int,
	tick time.Duration,
	gate *tickGate,
	stats *runStats,
) int64 {
	if p.DurationSeconds <= 0 || activeBots <= 0 {
		return 0
	}

	start := time.Now()
	deadline := time.NewTimer(time.Duration(p.DurationSeconds) * time.Second)
	defer deadline.Stop()

	ticker := time.NewTicker(tick)
	defer ticker.Stop()

	var minted int64
	for {
		select {
		case <-ctx.Done():
			return minted
		case <-deadline.C:
			return minted
		case <-ticker.C:
			elapsed := time.Since(start).Seconds()
			expected := int64(math.Floor(elapsed * p.RPS))
			missing := expected - minted
			if missing <= 0 {
				continue
			}

			normalPerTick := int64(math.Ceil(p.RPS * tick.Seconds()))
			toAdd := minInt64(missing, maxInt64(1, normalPerTick*2))
			gate.publishTick(activeBots, toAdd)
			minted += toAdd
		}
	}
}

// schedulerTick targets roughly 10% of the phase RPS per scheduler wakeup, as
// requested, while still allowing an env override for calibration experiments.
func schedulerTick(rps float64) time.Duration {
	if override := getenvInt("SCHEDULER_TICK_MS", 0); override > 0 {
		return time.Duration(override) * time.Millisecond
	}
	if rps <= 0 {
		return 100 * time.Millisecond
	}
	return 100 * time.Millisecond
}

func getCPUUsage() float64 {
	values, err := cpu.Percent(time.Second, false)
	if err != nil || len(values) == 0 {
		return 0
	}
	return values[0]
}

// launchAction prepares one request using the bot's local state, then sends it
// in a goroutine so response latency does not throttle request generation.
func (b *bot) launchAction(ctx context.Context) {
	plan := b.planAction()

	var payload []byte
	switch strings.ToUpper(plan.orderType) {
	case "CANCEL":
		payload = buildCancelFIX(plan)
	default:
		payload = buildNewOrderFIX(plan)
	}

	b.publishEvent(ctx, payload)
}

func (b *bot) publishEvent(ctx context.Context, payload any) bool {
	var value []byte
	switch v := payload.(type) {
	case []byte:
		value = append([]byte(nil), v...)
	case string:
		value = []byte(v)
	default:
		b.stats.failed.Add(1)
		log.Printf("REDPANDA PAYLOAD ERROR: unsupported payload type %T", payload)
		return false
	}

	if len(value) == 0 {
		b.stats.failed.Add(1)
		log.Printf("REDPANDA PAYLOAD ERROR: empty payload")
		return false
	}

	b.stats.sent.Add(1)

	writeCtx, cancel := context.WithTimeout(ctx, b.cfg.RequestTimeout)
	defer cancel()

	if err := b.writer.WriteMessages(writeCtx, kafka.Message{
		Key:   []byte(b.cfg.SubmissionID),
		Value: value,
	}); err != nil {
		b.stats.failed.Add(1)
		log.Printf("REDPANDA PUBLISH ERROR: %v", err)
		return false
	}

	b.stats.success.Add(1)
	return true
}

type fixField struct {
	tag   int
	value string
}

func buildNewOrderFIX(plan requestPlan) []byte {
	fields := []fixField{
		{tag: 35, value: "D"},
		{tag: 11, value: plan.orderID},
		{tag: 38, value: strconv.Itoa(plan.quantity)},
		{tag: 40, value: ordTypeToFIX(plan.orderType)},
		{tag: 54, value: sideToFIX(plan.side)},
		{tag: 55, value: plan.symbol},
		{tag: 59, value: "0"},
	}

	if strings.EqualFold(plan.orderType, "LIMIT") {
		fields = append(fields, fixField{tag: 44, value: strconv.FormatFloat(plan.price, 'f', 2, 64)})
	}

	return buildFIXEnvelope(fields)
}

func buildCancelFIX(plan requestPlan) []byte {
	fields := []fixField{
		{tag: 35, value: "F"},
		{tag: 11, value: plan.cancelOrderID},
		{tag: 41, value: plan.origClOrdID},
		{tag: 54, value: sideToFIX(plan.side)},
		{tag: 55, value: plan.symbol},
		{tag: 59, value: "0"},
	}

	if plan.quantity > 0 {
		fields = append(fields, fixField{tag: 38, value: strconv.Itoa(plan.quantity)})
	}

	return buildFIXEnvelope(fields)
}

func buildFIXEnvelope(fields []fixField) []byte {
	body := make([]byte, 0, 256)
	for _, field := range fields {
		body = appendFIXField(body, field.tag, field.value)
	}

	bodyLength := calculateBodyLength(body)
	msg := make([]byte, 0, len("8=FIX.4.2\x01")+len("9=000000\x01")+len(body)+len("10=000\x01"))
	msg = append(msg, []byte("8=FIX.4.2\x01")...)
	msg = append(msg, []byte(fmt.Sprintf("9=%d\x01", bodyLength))...)
	msg = append(msg, body...)

	checksum := calculateChecksum(msg)
	msg = append(msg, []byte("10="+checksum+"\x01")...)
	return msg
}

func appendFIXField(buf []byte, tag int, value string) []byte {
	buf = append(buf, []byte(fmt.Sprintf("%d=%s", tag, value))...)
	buf = append(buf, '\x01')
	return buf
}

func calculateBodyLength(body []byte) int {
	return len(body)
}

func calculateChecksum(msg []byte) string {
	sum := 0
	for _, b := range msg {
		sum += int(b)
	}
	return fmt.Sprintf("%03d", sum%256)
}

func sideToFIX(side string) string {
	switch strings.ToUpper(strings.TrimSpace(side)) {
	case "BUY":
		return "1"
	case "SELL":
		return "2"
	default:
		return "0"
	}
}

func ordTypeToFIX(orderType string) string {
	switch strings.ToUpper(strings.TrimSpace(orderType)) {
	case "MARKET":
		return "1"
	case "LIMIT":
		return "2"
	default:
		return "0"
	}
}

// planAction chooses limit, market, or cancel based on configured weights.
// The mutex protects rand/openOrders because HTTP requests now run async.
func (b *bot) planAction() requestPlan {
	b.mu.Lock()
	defer b.mu.Unlock()

	switch b.chooseAction() {
	case "cancel":
		if len(b.openOrders) > 0 {
			return b.planCancel()
		}
		return b.planLimit()
	case "market":
		return b.planMarket()
	default:
		return b.planLimit()
	}
}

func (b *bot) planLimit() requestPlan {
	orderID := b.newOrderID()
	side := b.randomSide()
	symbol := b.randomSymbol()
	price := b.randomPrice()
	quantity := b.randomQuantity()

	b.openOrders = append(b.openOrders, openOrder{
		OrderID:  orderID,
		Side:     side,
		Symbol:   symbol,
		Price:    price,
		Quantity: quantity,
	})

	return requestPlan{
		orderID:   orderID,
		orderType: "LIMIT",
		side:      side,
		symbol:    symbol,
		price:     price,
		quantity:  quantity,
	}
}

func (b *bot) planMarket() requestPlan {
	side := b.randomSide()
	symbol := b.randomSymbol()
	quantity := b.randomQuantity()

	return requestPlan{
		orderID:   b.newOrderID(),
		orderType: "MARKET",
		side:      side,
		symbol:    symbol,
		quantity:  quantity,
	}
}

func (b *bot) planCancel() requestPlan {
	index := b.rand.Intn(len(b.openOrders))
	order := b.openOrders[index]
	b.openOrders = append(b.openOrders[:index], b.openOrders[index+1:]...)

	cancelOrderID := b.newOrderID()
	return requestPlan{
		orderID:       cancelOrderID,
		orderType:     "CANCEL",
		side:          order.Side,
		symbol:        order.Symbol,
		price:         order.Price,
		quantity:      order.Quantity,
		origClOrdID:   order.OrderID,
		cancelOrderID: cancelOrderID,
	}
}

// sendJSON sends one REST request to the contestant service.
// It only tracks basic success/failure counters for now, not latency histograms.

func (b *bot) chooseAction() string {
	total := b.cfg.LimitWeight + b.cfg.MarketWeight + b.cfg.CancelWeight
	if total <= 0 {
		return "limit"
	}

	choice := b.rand.Intn(total)
	if choice < b.cfg.LimitWeight {
		return "limit"
	}
	choice -= b.cfg.LimitWeight
	if choice < b.cfg.MarketWeight {
		return "market"
	}
	return "cancel"
}

func (b *bot) newOrderID() string {
	return fmt.Sprintf("%s-bot-%d-%d", b.cfg.RunID, b.id, time.Now().UnixNano())
}

func (b *bot) randomSide() string {
	if b.rand.Intn(2) == 0 {
		return "BUY"
	}
	return "SELL"
}

func (b *bot) randomSymbol() string {
	return b.cfg.Symbols[b.rand.Intn(len(b.cfg.Symbols))]
}

func (b *bot) randomPrice() float64 {
	return 90 + b.rand.Float64()*20
}

func (b *bot) randomQuantity() int {
	return b.rand.Intn(100) + 1
}

func loadConfig() (config, error) {
	cfg := config{
		RunID:         getenv("RUN_ID", fmt.Sprintf("run-%d", time.Now().Unix())),
		RunnerID:      getenv("RUNNER_ID", getenv("HOSTNAME", fmt.Sprintf("runner-%d", time.Now().UnixNano()))),
		ControllerURL: strings.TrimRight(getenv("CONTROLLER_URL", ""), "/"),
		// NEW
		SubmissionID:     getenv("SUBMISSION_ID", "local-submission"),
		TopicName:        getenv("TOPIC_NAME", "orders-local"),
		RedpandaBrokers:  splitCSV(getenv("REDPANDA_BOOTSTRAP_SERVERS", "")),
		RedpandaUsername: getenv("REDPANDA_SASL_USERNAME", ""),
		RedpandaPassword: getenv("REDPANDA_SASL_PASSWORD", ""),

		BotsPerPod:        getenvInt("BOTS_PER_POD", 100),
		RequestTimeout:    time.Duration(getenvInt("REQUEST_TIMEOUT_SECONDS", 20)) * time.Second,
		StartPollInterval: time.Duration(getenvInt("START_POLL_INTERVAL_MS", 500)) * time.Millisecond,
		Symbols:           splitCSV(getenv("SYMBOLS", "AAPL,MSFT,GOOG")),
		LimitWeight:       getenvInt("LIMIT_WEIGHT", 60),
		MarketWeight:      getenvInt("MARKET_WEIGHT", 25),
		CancelWeight:      getenvInt("CANCEL_WEIGHT", 15),
	}
	if len(cfg.RedpandaBrokers) == 0 {
		return cfg, errors.New("REDPANDA_BROKERS is required")
	}
	if cfg.TopicName == "" {
		return cfg, errors.New("TOPIC_NAME is required")
	}
	if cfg.SubmissionID == "" {
		return cfg, errors.New("SUBMISSION_ID is required")
	}
	if cfg.BotsPerPod <= 0 {
		return cfg, errors.New("BOTS_PER_POD must be greater than zero")
	}
	if cfg.RequestTimeout <= 0 {
		return cfg, errors.New("REQUEST_TIMEOUT_SECONDS must be greater than zero")
	}
	if cfg.StartPollInterval <= 0 {
		return cfg, errors.New("START_POLL_INTERVAL_MS must be greater than zero")
	}
	if len(cfg.Symbols) == 0 {
		return cfg, errors.New("SYMBOLS must include at least one symbol")
	}

	phases, err := loadPhases(cfg.BotsPerPod)
	if err != nil {
		return cfg, err
	}
	cfg.Phases = phases

	return cfg, nil
}

func loadPhases(botsPerPod int) ([]phase, error) {
	raw := strings.TrimSpace(os.Getenv("PHASES_JSON"))
	log.Printf("RAW PHASES_JSON = %q", os.Getenv("PHASES_JSON"))
	if raw != "" {
		log.Printf("RAW ENV VALUE = %q", os.Getenv("PHASES_JSON"))
		var phases []phase
		if err := json.Unmarshal([]byte(raw), &phases); err != nil {
			return nil, fmt.Errorf("parse PHASES_JSON: %w", err)
		}
		return normalizePhases(phases, botsPerPod)
	}

	// duration := getenvInt("DURATION_SECONDS", 30)
	// activeBots := getenvInt("ACTIVE_BOTS", botsPerPod)
	// rps := getenvFloat("TARGET_RPS_PER_POD", float64(activeBots))

	return normalizePhases([]phase{{
		Name:            "steady",
		DurationSeconds: 20,
		ActiveBots:      100,
		RPS:             50000,
	}}, botsPerPod)
}

func normalizePhases(phases []phase, botsPerPod int) ([]phase, error) {
	if len(phases) == 0 {
		return nil, errors.New("at least one phase is required")
	}

	for i := range phases {
		if phases[i].Name == "" {
			phases[i].Name = fmt.Sprintf("phase-%d", i+1)
		}
		if phases[i].DurationSeconds <= 0 {
			return nil, fmt.Errorf("phase %q duration_seconds must be greater than zero", phases[i].Name)
		}
		if phases[i].ActiveBots < 0 {
			return nil, fmt.Errorf("phase %q active_bots cannot be negative", phases[i].Name)
		}
		if phases[i].ActiveBots > botsPerPod {
			phases[i].ActiveBots = botsPerPod
		}
		if phases[i].RPS < 0 {
			return nil, fmt.Errorf("phase %q rps cannot be negative", phases[i].Name)
		}
	}

	return phases, nil
}

func splitCSV(value string) []string {
	parts := strings.Split(value, ",")
	out := make([]string, 0, len(parts))
	for _, part := range parts {
		part = strings.TrimSpace(part)
		if part != "" {
			out = append(out, part)
		}
	}
	return out
}

func getenv(key, fallback string) string {
	value := strings.TrimSpace(os.Getenv(key))
	if value == "" {
		return fallback
	}
	return value
}

func getenvInt(key string, fallback int) int {
	value := strings.TrimSpace(os.Getenv(key))
	if value == "" {
		return fallback
	}
	parsed, err := strconv.Atoi(value)
	if err != nil {
		return fallback
	}
	return parsed
}

func getenvFloat(key string, fallback float64) float64 {
	value := strings.TrimSpace(os.Getenv(key))
	if value == "" {
		return fallback
	}
	parsed, err := strconv.ParseFloat(value, 64)
	if err != nil {
		return fallback
	}
	return parsed
}

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func maxFloat(a, b float64) float64 {
	if a > b {
		return a
	}
	return b
}

func minInt64(a, b int64) int64 {
	if a < b {
		return a
	}
	return b
}

func maxInt64(a, b int64) int64 {
	if a > b {
		return a
	}
	return b
}
