#include <gtest/gtest.h>
#include "metrics/metrics_collector.h"
#include <thread>
#include <chrono>

using namespace metrics;

class MetricsCollectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        MetricsCollector::getInstance().reset();
    }
};

TEST_F(MetricsCollectorTest, RecordOrderLatency) {
    auto& collector = MetricsCollector::getInstance();
    
    collector.recordOrderLatency(std::chrono::microseconds(100));
    collector.recordOrderLatency(std::chrono::microseconds(200));
    collector.recordOrderLatency(std::chrono::microseconds(150));
    
    auto stats = collector.getOrderLatencyStats();
    EXPECT_GT(stats.p50, 0.0);
    EXPECT_GT(stats.avg, 0.0);
}

TEST_F(MetricsCollectorTest, RecordOrders) {
    auto& collector = MetricsCollector::getInstance();
    
    collector.recordOrderSubmitted();
    collector.recordOrderSubmitted();
    collector.recordOrderFilled();
    collector.recordOrderRejected();
    
    auto metrics = collector.getMetrics();
    EXPECT_EQ(metrics.total_orders, 2);
    EXPECT_EQ(metrics.filled_orders, 1);
    EXPECT_EQ(metrics.rejected_orders, 1);
}

TEST_F(MetricsCollectorTest, CalculateFillRate) {
    auto& collector = MetricsCollector::getInstance();
    
    collector.recordOrderSubmitted();
    collector.recordOrderSubmitted();
    collector.recordOrderSubmitted();
    collector.recordOrderSubmitted();
    collector.recordOrderFilled();
    collector.recordOrderFilled();
    collector.recordOrderFilled();
    
    auto metrics = collector.getMetrics();
    EXPECT_NEAR(metrics.fill_rate, 0.75, 0.01);
}

TEST_F(MetricsCollectorTest, CalculateRejectionRate) {
    auto& collector = MetricsCollector::getInstance();
    
    collector.recordOrderSubmitted();
    collector.recordOrderSubmitted();
    collector.recordOrderSubmitted();
    collector.recordOrderSubmitted();
    collector.recordOrderRejected();
    
    auto metrics = collector.getMetrics();
    EXPECT_NEAR(metrics.rejection_rate, 0.25, 0.01);
}

TEST_F(MetricsCollectorTest, ActiveSessions) {
    auto& collector = MetricsCollector::getInstance();
    
    collector.incrementActiveSessions();
    collector.incrementActiveSessions();
    collector.incrementActiveSessions();
    
    auto metrics = collector.getMetrics();
    EXPECT_EQ(metrics.active_sessions, 3);
    
    collector.decrementActiveSessions();
    metrics = collector.getMetrics();
    EXPECT_EQ(metrics.active_sessions, 2);
}

TEST_F(MetricsCollectorTest, ActiveOrders) {
    auto& collector = MetricsCollector::getInstance();
    
    collector.setActiveOrders(42);
    
    auto metrics = collector.getMetrics();
    EXPECT_EQ(metrics.active_orders, 42);
}

TEST_F(MetricsCollectorTest, PrometheusExport) {
    auto& collector = MetricsCollector::getInstance();
    
    collector.recordOrderSubmitted();
    collector.recordOrderFilled();
    
    auto prometheus = collector.exportPrometheus();
    EXPECT_TRUE(prometheus.find("fix_orders_total") != std::string::npos);
    EXPECT_TRUE(prometheus.find("fix_orders_filled") != std::string::npos);
}

TEST_F(MetricsCollectorTest, LatencyPercentiles) {
    auto& collector = MetricsCollector::getInstance();
    
    for (int i = 1; i <= 100; ++i) {
        collector.recordOrderLatency(std::chrono::microseconds(i * 10));
    }
    
    auto stats = collector.getOrderLatencyStats();
    EXPECT_GT(stats.p50, 400.0);
    EXPECT_LT(stats.p50, 600.0);
    EXPECT_GT(stats.p95, 900.0);
    EXPECT_GT(stats.p99, 980.0);
}

TEST_F(MetricsCollectorTest, Reset) {
    auto& collector = MetricsCollector::getInstance();
    
    collector.recordOrderSubmitted();
    collector.recordOrderFilled();
    collector.reset();
    
    auto metrics = collector.getMetrics();
    EXPECT_EQ(metrics.total_orders, 0);
    EXPECT_EQ(metrics.filled_orders, 0);
}

TEST_F(MetricsCollectorTest, ExecutionLatency) {
    auto& collector = MetricsCollector::getInstance();
    
    collector.recordExecutionLatency(std::chrono::microseconds(50));
    collector.recordExecutionLatency(std::chrono::microseconds(100));
    
    auto stats = collector.getExecutionLatencyStats();
    EXPECT_GT(stats.avg, 0.0);
    EXPECT_GT(stats.max, 50.0);
}

TEST_F(MetricsCollectorTest, LatencyTimer) {
    LatencyTimer timer;
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    
    auto elapsed = timer.elapsedMicroseconds();
    EXPECT_GT(elapsed, 90.0);
}
