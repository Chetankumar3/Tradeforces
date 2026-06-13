#pragma once

#include <string>
#include <chrono>
#include <vector>
#include <atomic>
#include <cstdint>

namespace metrics {

struct LatencyStats {
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
    double avg = 0.0;
};

struct ThroughputStats {
    uint64_t total_count = 0;
    double per_second = 0.0;
    std::chrono::steady_clock::time_point last_update;
};

struct SystemMetrics {
    uint64_t active_sessions = 0;
    uint64_t total_orders = 0;
    uint64_t filled_orders = 0;
    uint64_t rejected_orders = 0;
    uint64_t canceled_orders = 0;
    uint64_t active_orders = 0;
    double fill_rate = 0.0;
    double rejection_rate = 0.0;
    LatencyStats order_latency;
    LatencyStats execution_latency;
    ThroughputStats order_throughput;
    ThroughputStats message_throughput;
};

using TimePoint = std::chrono::steady_clock::time_point;
using Duration = std::chrono::nanoseconds;

}
