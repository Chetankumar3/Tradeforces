#pragma once

#include "metrics_types.h"
#include <mutex>
#include <vector>
#include <string>
#include <unordered_map>

namespace metrics {

class MetricsCollector {
public:
    static MetricsCollector& getInstance();
    
    void recordOrderLatency(Duration latency);
    void recordExecutionLatency(Duration latency);
    void recordOrderSubmitted();
    void recordOrderFilled();
    void recordOrderRejected();
    void recordOrderCanceled();
    void recordMessageProcessed();
    
    void incrementActiveSessions();
    void decrementActiveSessions();
    void setActiveOrders(uint64_t count);
    
    SystemMetrics getMetrics() const;
    LatencyStats getOrderLatencyStats() const;
    LatencyStats getExecutionLatencyStats() const;
    
    std::string exportPrometheus() const;
    void reset();
    
private:
    MetricsCollector() = default;
    ~MetricsCollector() = default;
    MetricsCollector(const MetricsCollector&) = delete;
    MetricsCollector& operator=(const MetricsCollector&) = delete;
    
    LatencyStats calculateLatencyStats(const std::vector<double>& latencies) const;
    void updateThroughput(ThroughputStats& stats, uint64_t count);
    
    mutable std::mutex mutex_;
    std::atomic<uint64_t> active_sessions_{0};
    std::atomic<uint64_t> total_orders_{0};
    std::atomic<uint64_t> filled_orders_{0};
    std::atomic<uint64_t> rejected_orders_{0};
    std::atomic<uint64_t> canceled_orders_{0};
    std::atomic<uint64_t> active_orders_{0};
    std::atomic<uint64_t> messages_processed_{0};
    
    std::vector<double> order_latencies_;
    std::vector<double> execution_latencies_;
    
    ThroughputStats order_throughput_;
    ThroughputStats message_throughput_;
    
    static constexpr size_t MAX_LATENCY_SAMPLES = 10000;
};

class LatencyTimer {
public:
    LatencyTimer() : start_(std::chrono::steady_clock::now()) {}
    
    Duration elapsed() const {
        return std::chrono::steady_clock::now() - start_;
    }
    
    double elapsedMicroseconds() const {
        return std::chrono::duration<double, std::micro>(elapsed()).count();
    }
    
private:
    TimePoint start_;
};

}
