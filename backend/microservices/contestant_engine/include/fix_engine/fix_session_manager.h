#pragma once

#include "fix_session.h"
#include "utils/config_loader.h"
#include "order_manager/order_manager.h"
#include "market_data/simulated_exchange.h"
#include "admin/admin_server.h"
#include "persistence/message_store.h"
#include "metrics/metrics_collector.h"
#include <memory>
#include <unordered_map>
#include <vector>
#include <thread>
#include <functional>
#include <boost/asio.hpp>

namespace fix_gateway {
namespace fix_engine {

using ExecutionReportSink = std::function<void(const FIXMessage&)>;

class FIXSessionManager {
public:
    static FIXSessionManager& getInstance();
    
    void initialize(const std::vector<utils::SessionConfig>& configs);
    void setExecutionReportSink(ExecutionReportSink callback);
    void start();
    void stop();
    std::shared_ptr<FIXSession> getSession(const std::string& sessionId);
    std::shared_ptr<order_manager::OrderManager> getOrderManager(const std::string& sessionId);
    std::vector<std::string> getSessionIds() const;
    
    MarketData::SimulatedExchange& getExchange() { return exchange_; }
    
    bool isRunning() const;

    FIXSessionManager(const FIXSessionManager&) = delete;
    FIXSessionManager& operator=(const FIXSessionManager&) = delete;

private:
    FIXSessionManager() = default;
    
    boost::asio::io_context ioContext_;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;
    std::vector<std::thread> threadPool_;
    
    std::unordered_map<std::string, std::shared_ptr<FIXSession>> sessions_;
    std::unordered_map<std::string, std::shared_ptr<order_manager::OrderManager>> orderManagers_;
    ExecutionReportSink executionReportSink_;
    MarketData::SimulatedExchange exchange_;
    std::unique_ptr<admin::AdminServer> adminServer_;
    std::unique_ptr<persistence::MessageStore> messageStore_;
    std::atomic<bool> running_{false};
    
    void runIoContext();
};

}
}
