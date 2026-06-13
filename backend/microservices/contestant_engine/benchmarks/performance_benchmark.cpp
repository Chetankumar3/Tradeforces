#include "metrics/metrics_collector.h"
#include "order_manager/order_types.h"
#include "risk/risk_manager.h"
#include <iostream>
#include <chrono>
#include <random>
#include <vector>
#include <thread>

using namespace metrics;
using namespace fix_gateway::order_manager;
using namespace risk;

void latencyBenchmark() {
    std::cout << "\n=== Latency Benchmark ===" << std::endl;
    
    auto& collector = MetricsCollector::getInstance();
    collector.reset();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> latency_dist(50, 500);
    
    const int num_orders = 10000;
    
    for (int i = 0; i < num_orders; ++i) {
        LatencyTimer timer;
        std::this_thread::sleep_for(std::chrono::microseconds(latency_dist(gen)));
        collector.recordOrderLatency(timer.elapsed());
    }
    
    auto stats = collector.getOrderLatencyStats();
    
    std::cout << "Orders processed: " << num_orders << std::endl;
    std::cout << "Latency p50: " << stats.p50 << " μs" << std::endl;
    std::cout << "Latency p95: " << stats.p95 << " μs" << std::endl;
    std::cout << "Latency p99: " << stats.p99 << " μs" << std::endl;
    std::cout << "Latency avg: " << stats.avg << " μs" << std::endl;
    std::cout << "Latency max: " << stats.max << " μs" << std::endl;
}

void throughputBenchmark() {
    std::cout << "\n=== Throughput Benchmark ===" << std::endl;
    
    auto& collector = MetricsCollector::getInstance();
    collector.reset();
    
    const int num_orders = 50000;
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < num_orders; ++i) {
        collector.recordOrderSubmitted();
        if (i % 2 == 0) {
            collector.recordOrderFilled();
        } else if (i % 7 == 0) {
            collector.recordOrderRejected();
        }
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<double>(end - start).count();
    
    auto metrics = collector.getMetrics();
    
    std::cout << "Orders processed: " << num_orders << std::endl;
    std::cout << "Time elapsed: " << duration << " seconds" << std::endl;
    std::cout << "Throughput: " << (num_orders / duration) << " orders/sec" << std::endl;
    std::cout << "Fill rate: " << (metrics.fill_rate * 100) << "%" << std::endl;
    std::cout << "Rejection rate: " << (metrics.rejection_rate * 100) << "%" << std::endl;
}

void riskCheckBenchmark() {
    std::cout << "\n=== Risk Check Benchmark ===" << std::endl;
    
    auto& riskMgr = RiskManager::getInstance();
    
    RiskLimits limits;
    limits.max_order_quantity = 10000;
    limits.max_order_value = 1000000.0;
    limits.fat_finger_threshold = 0.10;
    riskMgr.setGlobalLimits(limits);
    
    const int num_checks = 100000;
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < num_checks; ++i) {
        Order order;
        order.symbol = "AAPL";
        order.side = Side::BUY;
        order.orderQty = 100;
        order.price = 150.0;
        order.account = "BENCH_ACCT";
        
        auto result = riskMgr.checkOrder(order, 150.0);
        (void)result;
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<double>(end - start).count();
    
    std::cout << "Risk checks: " << num_checks << std::endl;
    std::cout << "Time elapsed: " << duration << " seconds" << std::endl;
    std::cout << "Throughput: " << (num_checks / duration) << " checks/sec" << std::endl;
    std::cout << "Avg latency: " << (duration * 1000000 / num_checks) << " μs" << std::endl;
}

void memoryBenchmark() {
    std::cout << "\n=== Memory Benchmark ===" << std::endl;
    
    const int num_orders = 100000;
    std::vector<Order> orders;
    orders.reserve(num_orders);
    
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < num_orders; ++i) {
        Order order;
        order.orderId = "ORD" + std::to_string(i);
        order.clOrdId = "CL" + std::to_string(i);
        order.symbol = "AAPL";
        order.side = Side::BUY;
        order.orderQty = 100;
        order.price = 150.0;
        order.account = "TEST";
        orders.push_back(order);
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<double>(end - start).count();
    
    size_t memory_per_order = sizeof(Order);
    size_t total_memory_mb = (orders.size() * memory_per_order) / (1024 * 1024);
    
    std::cout << "Orders created: " << num_orders << std::endl;
    std::cout << "Time elapsed: " << duration << " seconds" << std::endl;
    std::cout << "Memory per order: " << memory_per_order << " bytes" << std::endl;
    std::cout << "Total memory: " << total_memory_mb << " MB" << std::endl;
    std::cout << "Creation rate: " << (num_orders / duration) << " orders/sec" << std::endl;
}

void concurrencyBenchmark() {
    std::cout << "\n=== Concurrency Benchmark ===" << std::endl;
    
    auto& collector = MetricsCollector::getInstance();
    collector.reset();
    
    const int num_threads = 8;
    const int orders_per_thread = 10000;
    
    std::vector<std::thread> threads;
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&collector, orders_per_thread]() {
            for (int j = 0; j < orders_per_thread; ++j) {
                LatencyTimer timer;
                collector.recordOrderSubmitted();
                collector.recordOrderLatency(timer.elapsed());
                
                if (j % 2 == 0) {
                    collector.recordOrderFilled();
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<double>(end - start).count();
    
    auto metrics = collector.getMetrics();
    
    std::cout << "Threads: " << num_threads << std::endl;
    std::cout << "Orders per thread: " << orders_per_thread << std::endl;
    std::cout << "Total orders: " << (num_threads * orders_per_thread) << std::endl;
    std::cout << "Time elapsed: " << duration << " seconds" << std::endl;
    std::cout << "Throughput: " << (metrics.total_orders / duration) << " orders/sec" << std::endl;
}

int main() {
    std::cout << "╔═══════════════════════════════════════╗" << std::endl;
    std::cout << "║  FIX Gateway Performance Benchmarks  ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════╝" << std::endl;
    
    latencyBenchmark();
    throughputBenchmark();
    riskCheckBenchmark();
    memoryBenchmark();
    concurrencyBenchmark();
    
    std::cout << "\n=== Benchmark Complete ===" << std::endl;
    
    return 0;
}
