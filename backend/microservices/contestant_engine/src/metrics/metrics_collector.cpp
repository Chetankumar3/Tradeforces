#include "../../include/metrics/metrics_collector.h"
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace metrics {

MetricsCollector& MetricsCollector::getInstance() {
    static MetricsCollector instance;
    return instance;
}

void MetricsCollector::recordOrderLatency(Duration latency) {
    double micros = std::chrono::duration<double, std::micro>(latency).count();
    std::lock_guard<std::mutex> lock(mutex_);
    order_latencies_.push_back(micros);
    if (order_latencies_.size() > MAX_LATENCY_SAMPLES) {
        order_latencies_.erase(order_latencies_.begin());
    }
}

void MetricsCollector::recordExecutionLatency(Duration latency) {
    double micros = std::chrono::duration<double, std::micro>(latency).count();
    std::lock_guard<std::mutex> lock(mutex_);
    execution_latencies_.push_back(micros);
    if (execution_latencies_.size() > MAX_LATENCY_SAMPLES) {
        execution_latencies_.erase(execution_latencies_.begin());
    }
}

void MetricsCollector::recordOrderSubmitted() {
    total_orders_.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);
    updateThroughput(order_throughput_, total_orders_.load());
}

void MetricsCollector::recordOrderFilled() {
    filled_orders_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsCollector::recordOrderRejected() {
    rejected_orders_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsCollector::recordOrderCanceled() {
    canceled_orders_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsCollector::recordMessageProcessed() {
    messages_processed_.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);
    updateThroughput(message_throughput_, messages_processed_.load());
}

void MetricsCollector::incrementActiveSessions() {
    active_sessions_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsCollector::decrementActiveSessions() {
    active_sessions_.fetch_sub(1, std::memory_order_relaxed);
}

void MetricsCollector::setActiveOrders(uint64_t count) {
    active_orders_.store(count, std::memory_order_relaxed);
}

SystemMetrics MetricsCollector::getMetrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    SystemMetrics metrics;
    metrics.active_sessions = active_sessions_.load();
    metrics.total_orders = total_orders_.load();
    metrics.filled_orders = filled_orders_.load();
    metrics.rejected_orders = rejected_orders_.load();
    metrics.canceled_orders = canceled_orders_.load();
    metrics.active_orders = active_orders_.load();
    
    if (metrics.total_orders > 0) {
        metrics.fill_rate = static_cast<double>(metrics.filled_orders) / metrics.total_orders;
        metrics.rejection_rate = static_cast<double>(metrics.rejected_orders) / metrics.total_orders;
    }
    
    metrics.order_latency = calculateLatencyStats(order_latencies_);
    metrics.execution_latency = calculateLatencyStats(execution_latencies_);
    metrics.order_throughput = order_throughput_;
    metrics.message_throughput = message_throughput_;
    
    return metrics;
}

LatencyStats MetricsCollector::getOrderLatencyStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return calculateLatencyStats(order_latencies_);
}

LatencyStats MetricsCollector::getExecutionLatencyStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return calculateLatencyStats(execution_latencies_);
}

LatencyStats MetricsCollector::calculateLatencyStats(const std::vector<double>& latencies) const {
    LatencyStats stats;
    if (latencies.empty()) return stats;
    
    std::vector<double> sorted = latencies;
    std::sort(sorted.begin(), sorted.end());
    
    size_t count = sorted.size();
    stats.p50 = sorted[count * 50 / 100];
    stats.p95 = sorted[count * 95 / 100];
    stats.p99 = sorted[count * 99 / 100];
    stats.max = sorted.back();
    stats.avg = std::accumulate(sorted.begin(), sorted.end(), 0.0) / count;
    
    return stats;
}

void MetricsCollector::updateThroughput(ThroughputStats& stats, uint64_t count) {
    auto now = std::chrono::steady_clock::now();
    if (stats.last_update.time_since_epoch().count() == 0) {
        stats.last_update = now;
        stats.total_count = count;
        return;
    }
    
    auto elapsed = std::chrono::duration<double>(now - stats.last_update).count();
    if (elapsed >= 1.0) {
        uint64_t delta = count - stats.total_count;
        stats.per_second = delta / elapsed;
        stats.total_count = count;
        stats.last_update = now;
    }
}

std::string MetricsCollector::exportPrometheus() const {
    auto metrics = getMetrics();
    std::ostringstream oss;
    
    oss << "# HELP fix_active_sessions Number of active FIX sessions\n";
    oss << "# TYPE fix_active_sessions gauge\n";
    oss << "fix_active_sessions " << metrics.active_sessions << "\n\n";
    
    oss << "# HELP fix_orders_total Total number of orders\n";
    oss << "# TYPE fix_orders_total counter\n";
    oss << "fix_orders_total " << metrics.total_orders << "\n\n";
    
    oss << "# HELP fix_orders_filled Number of filled orders\n";
    oss << "# TYPE fix_orders_filled counter\n";
    oss << "fix_orders_filled " << metrics.filled_orders << "\n\n";
    
    oss << "# HELP fix_orders_rejected Number of rejected orders\n";
    oss << "# TYPE fix_orders_rejected counter\n";
    oss << "fix_orders_rejected " << metrics.rejected_orders << "\n\n";
    
    oss << "# HELP fix_orders_canceled Number of canceled orders\n";
    oss << "# TYPE fix_orders_canceled counter\n";
    oss << "fix_orders_canceled " << metrics.canceled_orders << "\n\n";
    
    oss << "# HELP fix_orders_active Number of active orders\n";
    oss << "# TYPE fix_orders_active gauge\n";
    oss << "fix_orders_active " << metrics.active_orders << "\n\n";
    
    oss << "# HELP fix_fill_rate Order fill rate\n";
    oss << "# TYPE fix_fill_rate gauge\n";
    oss << "fix_fill_rate " << std::fixed << std::setprecision(4) << metrics.fill_rate << "\n\n";
    
    oss << "# HELP fix_rejection_rate Order rejection rate\n";
    oss << "# TYPE fix_rejection_rate gauge\n";
    oss << "fix_rejection_rate " << std::fixed << std::setprecision(4) << metrics.rejection_rate << "\n\n";
    
    oss << "# HELP fix_order_latency_microseconds Order processing latency\n";
    oss << "# TYPE fix_order_latency_microseconds summary\n";
    oss << "fix_order_latency_microseconds{quantile=\"0.5\"} " << metrics.order_latency.p50 << "\n";
    oss << "fix_order_latency_microseconds{quantile=\"0.95\"} " << metrics.order_latency.p95 << "\n";
    oss << "fix_order_latency_microseconds{quantile=\"0.99\"} " << metrics.order_latency.p99 << "\n";
    oss << "fix_order_latency_microseconds_sum " << metrics.order_latency.avg << "\n\n";
    
    oss << "# HELP fix_execution_latency_microseconds Execution latency\n";
    oss << "# TYPE fix_execution_latency_microseconds summary\n";
    oss << "fix_execution_latency_microseconds{quantile=\"0.5\"} " << metrics.execution_latency.p50 << "\n";
    oss << "fix_execution_latency_microseconds{quantile=\"0.95\"} " << metrics.execution_latency.p95 << "\n";
    oss << "fix_execution_latency_microseconds{quantile=\"0.99\"} " << metrics.execution_latency.p99 << "\n";
    oss << "fix_execution_latency_microseconds_sum " << metrics.execution_latency.avg << "\n\n";
    
    oss << "# HELP fix_order_throughput_per_second Orders processed per second\n";
    oss << "# TYPE fix_order_throughput_per_second gauge\n";
    oss << "fix_order_throughput_per_second " << metrics.order_throughput.per_second << "\n\n";
    
    oss << "# HELP fix_message_throughput_per_second Messages processed per second\n";
    oss << "# TYPE fix_message_throughput_per_second gauge\n";
    oss << "fix_message_throughput_per_second " << metrics.message_throughput.per_second << "\n\n";
    
    return oss.str();
}

void MetricsCollector::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    active_sessions_.store(0);
    total_orders_.store(0);
    filled_orders_.store(0);
    rejected_orders_.store(0);
    canceled_orders_.store(0);
    active_orders_.store(0);
    messages_processed_.store(0);
    order_latencies_.clear();
    execution_latencies_.clear();
    order_throughput_ = ThroughputStats{};
    message_throughput_ = ThroughputStats{};
}

}
