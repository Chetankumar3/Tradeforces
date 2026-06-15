package main

import (
	"testing"
)

func TestLoadConfigDefaultsToSupportedSymbols(t *testing.T) {
	t.Setenv("REDPANDA_BOOTSTRAP_SERVERS", "localhost:9092")
	t.Setenv("TOPIC_NAME", "orders-test")
	t.Setenv("SUBMISSION_ID", "submission-test")
	t.Setenv("BOTS_PER_POD", "1")
	t.Setenv("REQUEST_TIMEOUT_SECONDS", "1")
	t.Setenv("START_POLL_INTERVAL_MS", "1")
	t.Setenv("SYMBOLS", "")
	t.Setenv("PHASES_JSON", "")

	cfg, err := loadConfig()
	if err != nil {
		t.Fatalf("loadConfig() error = %v", err)
	}

	containsGOOG := false
	containsGOOGL := false
	for _, symbol := range cfg.Symbols {
		if symbol == "GOOG" {
			containsGOOG = true
		}
		if symbol == "GOOGL" {
			containsGOOGL = true
		}
	}

	if !containsGOOG {
		t.Fatalf("expected default symbols to include GOOG, got %v", cfg.Symbols)
	}

	if containsGOOGL {
		t.Fatalf("default symbols should not include GOOGL, got %v", cfg.Symbols)
	}
}
