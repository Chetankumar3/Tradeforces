package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"cloud.google.com/go/pubsub/v2"
	batchv1 "k8s.io/api/batch/v1"
	corev1 "k8s.io/api/core/v1"
	apierrors "k8s.io/apimachinery/pkg/api/errors"
	"k8s.io/apimachinery/pkg/api/resource"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/rest"
	"k8s.io/client-go/tools/clientcmd"
)

type config struct {
	Namespace           string
	ListenAddr          string
	ControllerURL       string
	RunnerPods          int
	BotsPerPod          int
	SchedulerTickMS     int
	StartDelay          time.Duration
	GCPProjectID        string
	PubSubSubscription  string
	RunnerMode          string
	RunnerImage         string
	RunnerCPURequest    string
	RunnerMemoryRequest string
	RunnerCPULimit      string
	RunnerMemoryLimit   string
	PhasesJSON          string

	// NEW: passed through to runner pods
	RedpandaBrokers  string
	RedpandaUsername string
	RedpandaPassword string
}

type queueJob struct {
	RunID        string
	SubmissionID string
	TeamID       string
	TopicName    string
	MicroVMPod   string
	TelemetryPod    string
	ShadowPod    string
}

type pubSubJobMessage struct {
	SubmissionID string `json:"submission_id"`
	TeamID       string `json:"team_id"`
	TopicName    string `json:"topic_name"`
	MicroVMPod   string `json:"microvm_pod_name"`
	TelemetryPod    string `json:"telemetry_pod_name"`
	ShadowPod    string `json:"shadow_pod_name"`
}

type readyRequest struct {
	RunID    string `json:"run_id"`
	RunnerID string `json:"runner_id"`
}

type startResponse struct {
	Ready   bool   `json:"ready"`
	StartAt string `json:"start_at,omitempty"`
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

type runState struct {
	mu              sync.Mutex
	runID           string
	expectedRunners int
	ready           map[string]time.Time
	results         map[string]resultRequest
	startAt         time.Time
	status          string
}

func main() {
	cfg, err := loadConfig()
	if err != nil {
		log.Fatalf("load config: %v", err)
	}

	client, err := newKubeClient()
	if err != nil {
		log.Fatalf("create kubernetes client: %v", err)
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	state := newRunState()
	server := newHTTPServer(cfg, state)

	go func() {
		log.Printf("benchmark-controller listening on %s", cfg.ListenAddr)
		if err := server.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			log.Fatalf("controller http server: %v", err)
		}
	}()

	if err := runQueueLoop(ctx, cfg, client, state); err != nil {
		log.Fatalf("controller loop: %v", err)
	}
}

func newRunState() *runState {
	return &runState{
		ready:   map[string]time.Time{},
		results: map[string]resultRequest{},
		status:  "idle",
	}
}

func (s *runState) begin(runID string, expectedRunners int) {
	s.mu.Lock()
	defer s.mu.Unlock()

	s.runID = runID
	s.expectedRunners = expectedRunners
	s.ready = map[string]time.Time{}
	s.results = map[string]resultRequest{}
	s.startAt = time.Time{}
	s.status = "waiting_for_runners"
}

func (s *runState) markReady(req readyRequest) (int, int, string) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if req.RunID != s.runID {
		return len(s.ready), s.expectedRunners, s.status
	}

	s.ready[req.RunnerID] = time.Now()
	return len(s.ready), s.expectedRunners, s.status
}

func (s *runState) readyCount() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return len(s.ready)
}

func (s *runState) setStartAt(startAt time.Time) {
	s.mu.Lock()
	defer s.mu.Unlock()

	s.startAt = startAt
	s.status = "scheduled"
}

func (s *runState) getStart(runID string) startResponse {
	s.mu.Lock()
	defer s.mu.Unlock()

	if runID != s.runID {
		return startResponse{Ready: false, Status: "unknown_run"}
	}
	if s.startAt.IsZero() {
		return startResponse{Ready: false, Status: s.status}
	}

	return startResponse{
		Ready:   true,
		StartAt: s.startAt.Format(time.RFC3339Nano),
		Status:  s.status,
	}
}

func (s *runState) markRunning() {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.status = "running"
}

func (s *runState) markResult(req resultRequest) int {
	s.mu.Lock()
	defer s.mu.Unlock()

	if req.RunID == s.runID {
		s.results[req.RunnerID] = req
	}
	return len(s.results)
}

func (s *runState) markStatus(status string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.status = status
}

func (s *runState) resultCount() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return len(s.results)
}

func newHTTPServer(cfg config, state *runState) *http.Server {
	mux := http.NewServeMux()

	mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
	})

	mux.HandleFunc("/ready", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			writeJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}

		var req readyRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid ready payload"})
			return
		}
		if req.RunID == "" || req.RunnerID == "" {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": "run_id and runner_id are required"})
			return
		}

		ready, expected, status := state.markReady(req)
		log.Printf("runner ready run_id=%s runner_id=%s ready=%d/%d", req.RunID, req.RunnerID, ready, expected)
		writeJSON(w, http.StatusOK, map[string]any{
			"status": status,
			"ready":  ready,
			"total":  expected,
		})
	})

	mux.HandleFunc("/start", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			writeJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}

		resp := state.getStart(r.URL.Query().Get("run_id"))
		writeJSON(w, http.StatusOK, resp)
	})

	mux.HandleFunc("/results", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			writeJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}

		var req resultRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid result payload"})
			return
		}

		count := state.markResult(req)
		log.Printf("runner result run_id=%s runner_id=%s sent=%d success=%d failed=%d dropped=%d results=%d/%d",
			req.RunID, req.RunnerID, req.Sent, req.Success, req.Failed, req.Dropped, count, cfg.RunnerPods)
		writeJSON(w, http.StatusOK, map[string]any{"status": "accepted", "results": count})
	})

	return &http.Server{
		Addr:    cfg.ListenAddr,
		Handler: mux,
	}
}

func runQueueLoop(ctx context.Context, cfg config, client kubernetes.Interface, state *runState) error {
	pubsubClient, err := pubsub.NewClient(ctx, cfg.GCPProjectID)
	if err != nil {
		return fmt.Errorf("create pubsub client: %w", err)
	}
	defer pubsubClient.Close()

	sub := pubsubClient.Subscriber(cfg.PubSubSubscription)

	// This controller runs scored benchmarks one at a time. Pub/Sub can deliver
	// many messages concurrently, but MaxOutstandingMessages=1 keeps cluster
	// capacity and result fairness simple: one queued submission owns the runner
	// pod set until its benchmark completes.
	sub.ReceiveSettings.MaxOutstandingMessages = 1
	sub.ReceiveSettings.NumGoroutines = 1

	log.Printf("listening for benchmark jobs project=%s subscription=%s", cfg.GCPProjectID, cfg.PubSubSubscription)
	return sub.Receive(ctx, func(msgCtx context.Context, msg *pubsub.Message) {
		job, err := parseQueueMessage(cfg, msg)
		if err != nil {
			// Invalid payloads will never become valid by retrying, so ack them
			// after logging instead of creating an infinite redelivery loop.
			log.Printf("dropping invalid pubsub message id=%s error=%v data=%s", msg.ID, err, string(msg.Data))
			msg.Ack()
			return
		}

		if err := processRun(msgCtx, cfg, client, state, job); err != nil {
			log.Printf("run failed run_id=%s submission_id=%s error=%v", job.RunID, job.SubmissionID, err)
			// Nack valid jobs when processing fails so Pub/Sub can redeliver the
			// same submission after capacity or transient Kubernetes issues clear.
			msg.Nack()
			return
		}

		msg.Ack()
	})
}

func parseQueueMessage(cfg config, msg *pubsub.Message) (queueJob, error) {
	var payload pubSubJobMessage
	if err := json.Unmarshal(msg.Data, &payload); err != nil {
		return queueJob{}, fmt.Errorf("decode pubsub data: %w", err)
	}

	payload.SubmissionID = strings.TrimSpace(payload.SubmissionID)
	payload.TeamID = strings.TrimSpace(payload.TeamID)
	payload.TopicName = strings.TrimSpace(payload.TopicName)
	payload.MicroVMPod = strings.TrimSpace(payload.MicroVMPod)
	payload.TelemetryPod = strings.TrimSpace(payload.TelemetryPod)
	payload.ShadowPod = strings.TrimSpace(payload.ShadowPod)
	if payload.SubmissionID == "" || payload.TeamID == "" || payload.TopicName == "" || payload.MicroVMPod == "" || payload.TelemetryPod == "" || payload.ShadowPod == "" {
		return queueJob{}, errors.New("submission_id, team_id, topic_name, microvm_pod_name, shadow_pod_name, and telemetry_pod_name are required")
	}

	return queueJob{
		RunID:        "run-" + payload.SubmissionID,
		SubmissionID: payload.SubmissionID,
		TeamID:       payload.TeamID,
		TopicName:    payload.TopicName,
		MicroVMPod:   payload.MicroVMPod,
		TelemetryPod: payload.TelemetryPod,
		ShadowPod:    payload.ShadowPod,
	}, nil
}

func processRun(ctx context.Context, cfg config, client kubernetes.Interface, state *runState, queued queueJob) error {
	log.Printf(
		"starting queued benchmark run_id=%s submission_id=%s topic=%s  runner_pods=%d",
		queued.RunID, queued.SubmissionID, queued.TopicName, cfg.RunnerPods,
	)

	if err := checkSchedulableCapacity(ctx, cfg, client); err != nil {
		return err
	}

	state.begin(queued.RunID, cfg.RunnerPods)

	job := buildRunnerJob(cfg, queued, queued.TopicName)
	if err := deleteJobIfExists(ctx, client, cfg.Namespace, job.Name); err != nil {
		return err
	}

	if _, err := client.BatchV1().Jobs(cfg.Namespace).Create(ctx, job, metav1.CreateOptions{}); err != nil {
		return fmt.Errorf("create runner job: %w", err)
	}
	log.Printf("created runner job name=%s run_id=%s", job.Name, queued.RunID)

	if err := waitForAllRunnersReady(ctx, cfg, client, state, job.Name); err != nil {
		_ = deleteJobIfExists(ctx, client, cfg.Namespace, job.Name)
		state.markStatus("invalid")
		return err
	}

	startAt := time.Now().Add(cfg.StartDelay)
	state.setStartAt(startAt)
	log.Printf("all runners ready run_id=%s start_at=%s", queued.RunID, startAt.Format(time.RFC3339Nano))

	waitUntil(ctx, startAt)
	state.markRunning()

	if err := waitForJobCompletion(ctx, cfg, client, state, job.Name); err != nil {
		_ = deleteJobIfExists(ctx, client, cfg.Namespace, job.Name)
		state.markStatus("invalid")
		return err
	}
	if state.resultCount() != cfg.RunnerPods {
		return fmt.Errorf(
			"missing results got=%d expected=%d",
			state.resultCount(),
			cfg.RunnerPods,
		)
	}

	state.markStatus("completed")

	log.Printf(
		"benchmark completed run_id=%s results=%d/%d",
		queued.RunID,
		state.resultCount(),
		cfg.RunnerPods,
	)

	if err := stopBenchmarkPods(
		ctx,
		client,
		cfg.Namespace,
		queued,
	); err != nil {

		return fmt.Errorf(
			"stop benchmark pods: %w",
			err,
		)
	}
	if err := deleteJobIfExists(
		ctx,
		client,
		cfg.Namespace,
		job.Name,
	); err != nil {
		return fmt.Errorf(
			"delete runner job: %w",
			err,
		)
	}

	log.Printf(
		"benchmark resources cleaned submission_id=%s",
		queued.SubmissionID,
	)

	return nil
}

func waitForAllRunnersReady(ctx context.Context, cfg config, client kubernetes.Interface, state *runState, jobName string) error {
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()

	for {
		if state.readyCount() == cfg.RunnerPods {
			return nil
		}
		if failed, err := jobFailed(ctx, cfg, client, jobName); err != nil {
			return err
		} else if failed {
			return errors.New("runner job failed before all pods became ready")
		}

		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-ticker.C:
		}
	}
}

func waitForJobCompletion(ctx context.Context, cfg config, client kubernetes.Interface, state *runState, jobName string) error {
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()

	for {
		job, err := client.BatchV1().Jobs(cfg.Namespace).Get(ctx, jobName, metav1.GetOptions{})
		if err != nil {
			return fmt.Errorf("get runner job: %w", err)
		}

		if job.Status.Failed > 0 {
			return fmt.Errorf("runner pod failed; benchmark invalid failed=%d", job.Status.Failed)
		}
		if job.Status.Succeeded == int32(cfg.RunnerPods) {
			return nil
		}

		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-ticker.C:
		}
	}
}

func jobFailed(ctx context.Context, cfg config, client kubernetes.Interface, jobName string) (bool, error) {
	job, err := client.BatchV1().Jobs(cfg.Namespace).Get(ctx, jobName, metav1.GetOptions{})
	if err != nil {
		return false, fmt.Errorf("get runner job: %w", err)
	}
	return job.Status.Failed > 0, nil
}

func buildRunnerJob(cfg config, queued queueJob, topicName string) *batchv1.Job {
	backoffLimit := int32(0)
	parallelism := int32(cfg.RunnerPods)
	completions := int32(cfg.RunnerPods)
	jobName := "bot-runner-" + safeName(queued.RunID)

	labels := map[string]string{
		"app":    "bot-runner",
		"run_id": safeName(queued.RunID),
	}

	return &batchv1.Job{
		ObjectMeta: metav1.ObjectMeta{
			Name:      jobName,
			Namespace: cfg.Namespace,
			Labels:    labels,
		},
		Spec: batchv1.JobSpec{
			BackoffLimit: &backoffLimit,
			Parallelism:  &parallelism,
			Completions:  &completions,
			Template: corev1.PodTemplateSpec{
				ObjectMeta: metav1.ObjectMeta{Labels: labels},
				Spec: corev1.PodSpec{
					RestartPolicy: corev1.RestartPolicyNever,
					Containers: []corev1.Container{
						buildRunnerContainer(cfg, queued, topicName),
					},
					Volumes: runnerVolumes(cfg),
				},
			},
		},
	}
}

func buildRunnerContainer(cfg config, queued queueJob, topicName string) corev1.Container {
	container := corev1.Container{
		Name:            "bot-runner",
		Image:           cfg.RunnerImage,
		ImagePullPolicy: corev1.PullIfNotPresent,
		Env: []corev1.EnvVar{
			{Name: "RUN_ID", Value: queued.RunID},
			{Name: "SUBMISSION_ID", Value: queued.SubmissionID},      // NEW
			{Name: "TOPIC_NAME", Value: topicName},                   // NEW
			{Name: "REDPANDA_BOOTSTRAP_SERVERS", Value: cfg.RedpandaBrokers},   // NEW
			{Name: "REDPANDA_SASL_USERNAME", Value: cfg.RedpandaUsername}, // NEW
			{Name: "REDPANDA_SASL_PASSWORD", Value: cfg.RedpandaPassword}, // NEW

			{Name: "CONTROLLER_URL", Value: cfg.ControllerURL},
			{Name: "BOTS_PER_POD", Value: strconv.Itoa(cfg.BotsPerPod)},
			{Name: "SCHEDULER_TICK_MS", Value: strconv.Itoa(cfg.SchedulerTickMS)},
			{Name: "PHASES_JSON", Value: cfg.PhasesJSON},
		},
		Resources: corev1.ResourceRequirements{
			Requests: corev1.ResourceList{
				corev1.ResourceCPU:    resource.MustParse(cfg.RunnerCPURequest),
				corev1.ResourceMemory: resource.MustParse(cfg.RunnerMemoryRequest),
			},
			Limits: corev1.ResourceList{
				corev1.ResourceCPU:    resource.MustParse(cfg.RunnerCPULimit),
				corev1.ResourceMemory: resource.MustParse(cfg.RunnerMemoryLimit),
			},
		},
	}
	return container
}

func runnerVolumes(cfg config) []corev1.Volume {
	if cfg.RunnerMode != "source" {
		return nil
	}
	return []corev1.Volume{
		{
			Name: "bot-runner-src",
			VolumeSource: corev1.VolumeSource{
				ConfigMap: &corev1.ConfigMapVolumeSource{
					LocalObjectReference: corev1.LocalObjectReference{Name: "bot-runner-src"},
				},
			},
		},
	}
}

func checkSchedulableCapacity(ctx context.Context, cfg config, client kubernetes.Interface) error {
	nodes, err := client.CoreV1().Nodes().List(ctx, metav1.ListOptions{})
	if err != nil {
		return fmt.Errorf("list nodes for capacity check: %w", err)
	}
	pods, err := client.CoreV1().Pods("").List(ctx, metav1.ListOptions{})
	if err != nil {
		return fmt.Errorf("list pods for capacity check: %w", err)
	}

	var allocCPU, allocMemory int64
	for _, node := range nodes.Items {
		allocCPU += node.Status.Allocatable.Cpu().MilliValue()
		allocMemory += node.Status.Allocatable.Memory().Value()
	}

	var usedCPU, usedMemory int64
	for _, pod := range pods.Items {
		if pod.Status.Phase == corev1.PodSucceeded || pod.Status.Phase == corev1.PodFailed {
			continue
		}
		for _, container := range pod.Spec.Containers {
			usedCPU += container.Resources.Requests.Cpu().MilliValue()
			usedMemory += container.Resources.Requests.Memory().Value()
		}
	}

	runnerCPURequest := resource.MustParse(cfg.RunnerCPURequest)
	runnerMemoryRequest := resource.MustParse(cfg.RunnerMemoryRequest)
	requiredCPU := int64(cfg.RunnerPods) * runnerCPURequest.MilliValue()
	requiredMemory := int64(cfg.RunnerPods) * runnerMemoryRequest.Value()
	availableCPU := allocCPU - usedCPU
	availableMemory := allocMemory - usedMemory

	log.Printf("capacity check available_cpu_m=%d required_cpu_m=%d available_memory_bytes=%d required_memory_bytes=%d",
		availableCPU, requiredCPU, availableMemory, requiredMemory)

	if availableCPU < requiredCPU || availableMemory < requiredMemory {
		return fmt.Errorf("not enough schedulable capacity for all runner pods")
	}
	return nil
}

func deleteJobIfExists(ctx context.Context, client kubernetes.Interface, namespace, name string) error {
	propagation := metav1.DeletePropagationBackground
	err := client.BatchV1().Jobs(namespace).Delete(ctx, name, metav1.DeleteOptions{PropagationPolicy: &propagation})
	if apierrors.IsNotFound(err) {
		return nil
	}
	return err
}
func deleteDeploymentIfExists(
	ctx context.Context,
	client kubernetes.Interface,
	namespace string,
	DeplymentName string,
) error {

	propagation := metav1.DeletePropagationForeground

	err := client.AppsV1().
		Deployments(namespace).
		Delete(
			ctx,
			DeplymentName,
			metav1.DeleteOptions{
				PropagationPolicy: &propagation,
			},
		)

	if apierrors.IsNotFound(err) {
		return nil
	}

	return err
}
func waitForPodDeletion(
	ctx context.Context,
	client kubernetes.Interface,
	namespace string,
	podName string,
) error {

	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()

	for {

		_, err := client.AppsV1().
			Deployments(namespace).
			Get(
				ctx,
				podName,
				metav1.GetOptions{},
			)

		if apierrors.IsNotFound(err) {
			return nil
		}

		if err != nil {
			return err
		}

		select {
		case <-ctx.Done():
			return ctx.Err()

		case <-ticker.C:
		}
	}
}
func stopBenchmarkPods(
	ctx context.Context,
	client kubernetes.Interface,
	namespace string,
	queued queueJob,
) error {

	log.Printf(
		"stopping benchmark pods submission=%s microvm=%s telemetry=%s",
		queued.SubmissionID,
		queued.MicroVMPod,
		queued.TelemetryPod,
	)

	if err := deleteDeploymentIfExists(
		ctx,
		client,
		namespace,
		queued.MicroVMPod,
	); err != nil {
		return fmt.Errorf(
			"delete microvm pod: %w",
			err,
		)
	}

	if err := deleteDeploymentIfExists(
		ctx,
		client,
		namespace,
		queued.TelemetryPod,
	); err != nil {
		return fmt.Errorf(
			"delete telemetry pod: %w",
			err,
		)
	}
	if err := deleteDeploymentIfExists(ctx, client, namespace, queued.ShadowPod); err != nil {
		return fmt.Errorf("delete shadow pod: %w", err)
	}

	if err := waitForPodDeletion(
		ctx,
		client,
		namespace,
		queued.MicroVMPod,
	); err != nil {
		return err
	}

	if err := waitForPodDeletion(
		ctx,
		client,
		namespace,
		queued.TelemetryPod,
	); err != nil {
		return err
	}

	if err := waitForPodDeletion(
		ctx,
		client,
		namespace,
		queued.ShadowPod,
	); err != nil {
		return err
	}

	return nil
}

// incluster config gives credentials, cfg has all the credentials, when i talk to api using clientset, _ := kubernetes.NewForConfig(config), the clientset has all the credentails , goes to api server, The API server validates that token's signature and maps it to a ServiceAccount. Authentication is based on the ServiceAccount token, not the pod name itself.
func newKubeClient() (kubernetes.Interface, error) {
	cfg, err := rest.InClusterConfig()
	if err != nil {
		kubeconfig := filepath.Join(os.Getenv("USERPROFILE"), ".kube", "config")
		if home := os.Getenv("HOME"); home != "" {
			kubeconfig = filepath.Join(home, ".kube", "config")
		}
		cfg, err = clientcmd.BuildConfigFromFlags("", kubeconfig)
		if err != nil {
			return nil, err
		}
	}
	return kubernetes.NewForConfig(cfg)
}

func loadConfig() (config, error) {
	cfg := config{
		Namespace:           getenv("NAMESPACE", currentNamespace()),
		ListenAddr:          getenv("LISTEN_ADDR", ":8080"),
		ControllerURL:       getenv("CONTROLLER_URL", "http://benchmark-controller:8080"),
		RunnerPods:          getenvInt("RUNNER_PODS", 3),
		BotsPerPod:          getenvInt("BOTS_PER_POD", 100),
		SchedulerTickMS:     getenvInt("SCHEDULER_TICK_MS", 20),
		StartDelay:          time.Duration(getenvInt("START_DELAY_MS", 5000)) * time.Millisecond,
		GCPProjectID:        strings.TrimSpace(os.Getenv("GCP_PROJECT_ID")),
		PubSubSubscription:  getenv("QUEUE2_SUBSCRIPTION_NAME", "queue2-sub"),
		RunnerMode:          getenv("RUNNER_MODE", "source"),
		RunnerImage:         getenv("RUNNER_IMAGE", "bot-runner:latest"),
		RunnerCPURequest:    getenv("RUNNER_CPU_REQUEST", "500m"),
		RunnerMemoryRequest: getenv("RUNNER_MEMORY_REQUEST", "256Mi"),
		RunnerCPULimit:      getenv("RUNNER_CPU_LIMIT", "1"),
		RunnerMemoryLimit:   getenv("RUNNER_MEMORY_LIMIT", "768Mi"),
		PhasesJSON:          getenv("PHASES_JSON", `[{"name":"steady","duration_seconds":20,"active_bots":100,"rps":500}]`),
		RedpandaBrokers:     getenv("REDPANDA_BOOTSTRAP_SERVERS", ""),
		RedpandaUsername:    getenv("REDPANDA_SASL_USERNAME", ""),
		RedpandaPassword:    getenv("REDPANDA_SASL_PASSWORD", ""),
	}
	if cfg.RedpandaBrokers == "" {
		return cfg, errors.New("REDPANDA_BOOTSTRAP_SERVERS is required")
	}
	if cfg.RunnerPods <= 0 {
		return cfg, errors.New("RUNNER_PODS must be greater than zero")
	}
	if cfg.BotsPerPod <= 0 {
		return cfg, errors.New("BOTS_PER_POD must be greater than zero")
	}
	if cfg.StartDelay <= 0 {
		return cfg, errors.New("START_DELAY_MS must be greater than zero")
	}
	if cfg.GCPProjectID == "" {
		return cfg, errors.New("GCP_PROJECT_ID is required")
	}
	if cfg.PubSubSubscription == "" {
		return cfg, errors.New("QUEUE2_SUBSCRIPTION_NAME is required")
	}

	modes := []string{"source", "image"}
	if !contains(modes, cfg.RunnerMode) {
		return cfg, fmt.Errorf("RUNNER_MODE must be one of %v", modes)
	}

	return cfg, nil
}

func currentNamespace() string {
	data, err := os.ReadFile("/var/run/secrets/kubernetes.io/serviceaccount/namespace")
	if err != nil {
		return "default"
	}
	return strings.TrimSpace(string(data))
}

func waitUntil(ctx context.Context, target time.Time) {
	timer := time.NewTimer(time.Until(target))
	defer timer.Stop()

	select {
	case <-ctx.Done():
	case <-timer.C:
	}
}

func writeJSON(w http.ResponseWriter, status int, payload any) {
	var buf bytes.Buffer
	_ = json.NewEncoder(&buf).Encode(payload)
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_, _ = w.Write(buf.Bytes())
}

func safeName(value string) string {
	value = strings.ToLower(value)
	var b strings.Builder
	for _, r := range value {
		if (r >= 'a' && r <= 'z') || (r >= '0' && r <= '9') || r == '-' {
			b.WriteRune(r)
			continue
		}
		b.WriteRune('-')
	}
	return strings.Trim(b.String(), "-")
}

func contains(values []string, value string) bool {
	sort.Strings(values)
	i := sort.SearchStrings(values, value)
	return i < len(values) && values[i] == value
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
