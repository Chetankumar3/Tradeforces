package main

import (
	"os"
	"testing"
	"time"
)

func TestBuildRunnerContainer_UsesConfiguredControllerURL(t *testing.T) {
	cfg := config{
		ControllerPodIP:     "172.20.0.243",
		ListenAddr:          ":8080",
		RunnerImage:         "bot-runner:latest",
		RunnerCPURequest:    "500m",
		RunnerMemoryRequest: "256Mi",
		RunnerCPULimit:      "1",
		RunnerMemoryLimit:   "768Mi",
	}

	container := buildRunnerContainer(cfg, queueJob{
		RunID:        "run-17",
		SubmissionID: "17",
	}, "17")

	for _, envVar := range container.Env {
		if envVar.Name == "CONTROLLER_URL" {
			want := "http://172.20.0.243:8080"
			if envVar.Value != want {
				t.Fatalf("CONTROLLER_URL = %q, want %q", envVar.Value, want)
			}
			return
		}
	}

	t.Fatal("CONTROLLER_URL env var not found")
}

func TestSubmissionServiceName(t *testing.T) {
	if got := submissionServiceName("20"); got != "submission-20" {
		t.Fatalf("submissionServiceName() = %q, want %q", got, "submission-20")
	}
}

func TestLoadConfig_UsesResetTTL(t *testing.T) {
	oldEnv := map[string]*string{}
	for _, key := range []string{
		"K3S_NAMESPACE",
		"LISTEN_ADDR",
		"RUNNER_PODS",
		"BOTS_PER_POD",
		"SCHEDULER_TICK_MS",
		"START_DELAY_MS",
		"PROJECT_ID",
		"QUEUE2_SUBSCRIPTION_NAME",
		"RUNNER_MODE",
		"RUNNER_IMAGE",
		"RUNNER_CPU_REQUEST",
		"RUNNER_MEMORY_REQUEST",
		"RUNNER_CPU_LIMIT",
		"RUNNER_MEMORY_LIMIT",
		"PHASES_JSON",
		"REDPANDA_BOOTSTRAP_SERVERS",
		"REDPANDA_SASL_USERNAME",
		"REDPANDA_SASL_PASSWORD",
		"RESET_TTL",
	} {
		if v, ok := os.LookupEnv(key); ok {
			value := v
			oldEnv[key] = &value
		} else {
			oldEnv[key] = nil
		}
		_ = os.Unsetenv(key)
	}
	defer func() {
		for key, value := range oldEnv {
			if value == nil {
				_ = os.Unsetenv(key)
			} else {
				_ = os.Setenv(key, *value)
			}
		}
	}()

	_ = os.Setenv("PROJECT_ID", "test-project")
	_ = os.Setenv("QUEUE2_SUBSCRIPTION_NAME", "queue2-sub")
	_ = os.Setenv("RUNNER_MODE", "source")
	_ = os.Setenv("RUNNER_IMAGE", "bot-runner:latest")
	_ = os.Setenv("RUNNER_PODS", "1")
	_ = os.Setenv("BOTS_PER_POD", "1")
	_ = os.Setenv("START_DELAY_MS", "100")
	_ = os.Setenv("REDPANDA_BOOTSTRAP_SERVERS", "localhost:9092")
	_ = os.Setenv("RESET_TTL", "5s")

	cfg, err := loadConfig()
	if err != nil {
		t.Fatalf("loadConfig() error = %v", err)
	}
	if cfg.ResetTTL != 5*time.Second {
		t.Fatalf("loadConfig().ResetTTL = %v, want %v", cfg.ResetTTL, 5*time.Second)
	}
}

func TestLoadConfig_PopulatesRunnerImage(t *testing.T) {
	oldEnv := map[string]*string{}
	for _, key := range []string{
		"K3S_NAMESPACE",
		"LISTEN_ADDR",
		"CONTROLLER_URL",
		"RUNNER_PODS",
		"BOTS_PER_POD",
		"SCHEDULER_TICK_MS",
		"START_DELAY_MS",
		"PROJECT_ID",
		"QUEUE2_SUBSCRIPTION_NAME",
		"RUNNER_MODE",
		"RUNNER_IMAGE",
		"RUNNER_CPU_REQUEST",
		"RUNNER_MEMORY_REQUEST",
		"RUNNER_CPU_LIMIT",
		"RUNNER_MEMORY_LIMIT",
		"PHASES_JSON",
		"REDPANDA_BOOTSTRAP_SERVERS",
		"REDPANDA_SASL_USERNAME",
		"REDPANDA_SASL_PASSWORD",
	} {
		if v, ok := os.LookupEnv(key); ok {
			value := v
			oldEnv[key] = &value
		} else {
			oldEnv[key] = nil
		}
		_ = os.Unsetenv(key)
	}
	defer func() {
		for key, value := range oldEnv {
			if value == nil {
				_ = os.Unsetenv(key)
			} else {
				_ = os.Setenv(key, *value)
			}
		}
	}()

	_ = os.Setenv("PROJECT_ID", "test-project")
	_ = os.Setenv("QUEUE2_SUBSCRIPTION_NAME", "queue2-sub")
	_ = os.Setenv("RUNNER_MODE", "source")
	_ = os.Setenv("RUNNER_IMAGE", "bot-runner:latest")
	_ = os.Setenv("RUNNER_PODS", "1")
	_ = os.Setenv("BOTS_PER_POD", "1")
	_ = os.Setenv("START_DELAY_MS", "100")
	_ = os.Setenv("REDPANDA_BOOTSTRAP_SERVERS", "localhost:9092")

	cfg, err := loadConfig()
	if err != nil {
		t.Fatalf("loadConfig() error = %v", err)
	}

	if cfg.RunnerImage != "bot-runner:latest" {
		t.Fatalf("loadConfig() runner image = %q, want %q", cfg.RunnerImage, "bot-runner:latest")
	}
}
