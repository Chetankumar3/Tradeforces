#include "fix_engine/fix_session_manager.h"
#include "utils/logger.h"

namespace fix_gateway {
namespace fix_engine {

FIXSessionManager& FIXSessionManager::getInstance() {
    static FIXSessionManager instance;
    return instance;
}

void FIXSessionManager::setExecutionReportSink(ExecutionReportSink callback) {
    executionReportSink_ = std::move(callback);
}

void FIXSessionManager::initialize(const std::vector<utils::SessionConfig>& configs) {
    auto& logger = utils::Logger::getInstance();
    logger.getLogger("main")->info("Initializing " + std::to_string(configs.size()) + " FIX session(s)");
    
    auto& configLoader = utils::ConfigLoader::getInstance();
    auto tradingRules = configLoader.getTradingRules();
    
    messageStore_ = std::make_unique<persistence::MessageStore>("./store");
    adminServer_ = std::make_unique<admin::AdminServer>(8080);
    
    risk::RiskManager::getInstance().loadConfiguration("config/risk_config.json");
    
    exchange_.loadSymbols("config/symbols.json");
    exchange_.initializePrices();
    
    exchange_.setMarketDataCallback([this](const std::string& sessionId, const fix_gateway::fix_engine::FIXMessage& mdMsg) {
        auto session = getSession(sessionId);
        if (session) {
            session->sendMessage(mdMsg);
        }
    });
    
    exchange_.setFillCallback([this](const MarketData::Fill& fill) {
        for (const auto& [sessionId, orderMgr] : orderManagers_) {
            auto buyOrder = orderMgr->getOrder(fill.buyOrderId);
            auto sellOrder = orderMgr->getOrder(fill.sellOrderId);
            
            if (buyOrder) {
                orderMgr->simulateFill(fill.buyOrderId, fill.quantity, fill.price,
                                       fill.aggressorOrderId == fill.buyOrderId);
            }
            if (sellOrder) {
                orderMgr->simulateFill(fill.sellOrderId, fill.quantity, fill.price,
                                       fill.aggressorOrderId == fill.sellOrderId);
            }
        }
    });
    
    for (const auto& config : configs) {
        auto session = std::make_shared<FIXSession>(config, ioContext_);
        sessions_[config.sessionId] = session;
        
        auto orderManager = std::make_shared<order_manager::OrderManager>(tradingRules);
        orderManagers_[config.sessionId] = orderManager;
        
        orderManager->setExecutionReportCallback([this, session](const fix_engine::FIXMessage& report) {
            if (executionReportSink_) {
                executionReportSink_(report);
            } else {
                session->sendMessage(report);
            }
        });
        
        orderManager->setOrderSubmitCallback([this](const std::shared_ptr<fix_gateway::order_manager::Order>& order) {
            exchange_.submitOrder(order);
        });
        
        session->setMarketDataCallback([this](const std::string& sessionId, const fix_gateway::fix_engine::FIXMessage& request) {
            exchange_.handleMarketDataRequest(sessionId, request);
        });
        
        session->setOrderManager(orderManager);
        
        logger.getLogger("main")->info("Initialized session: " + config.sessionId);
    }
}

void FIXSessionManager::startExecutionOnly() {
    if (running_) {
        return;
    }

    auto& logger = utils::Logger::getInstance();
    logger.getLogger("main")->info("Starting execution-only FIX pipeline (FIX sessions remain disabled)");

    if (!adminServer_) {
        adminServer_ = std::make_unique<admin::AdminServer>(8080);
    }

    adminServer_->start();
    exchange_.start();
    running_ = true;

    logger.getLogger("main")->info("Execution-only pipeline started");
}

void FIXSessionManager::start() {
    if (running_) {
        return;
    }
    
    auto& logger = utils::Logger::getInstance();
    logger.getLogger("main")->info("Starting FIX Session Manager");
    
    work_ = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(boost::asio::make_work_guard(ioContext_));
    
    adminServer_->start();
    exchange_.start();
    
    for (auto& [sessionId, session] : sessions_) {
        session->start();
        metrics::MetricsCollector::getInstance().incrementActiveSessions();
    }
    
    unsigned int numThreads = std::max(2u, std::thread::hardware_concurrency());
    logger.getLogger("main")->info("Starting " + std::to_string(numThreads) + " I/O threads");
    
    for (unsigned int i = 0; i < numThreads; ++i) {
        threadPool_.emplace_back([this]() { runIoContext(); });
    }
    
    running_ = true;
    logger.getLogger("main")->info("FIX Session Manager started");
}

void FIXSessionManager::stop() {
    if (!running_) {
        return;
    }
    
    auto& logger = utils::Logger::getInstance();
    logger.getLogger("main")->info("Stopping FIX Session Manager");
    
    adminServer_->stop();
    exchange_.stop();
    
    for (auto& [sessionId, session] : sessions_) {
        session->stop();
        metrics::MetricsCollector::getInstance().decrementActiveSessions();
    }
    
    messageStore_->flush();
    
    work_.reset();
    ioContext_.stop();
    
    for (auto& thread : threadPool_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    threadPool_.clear();
    running_ = false;
    
    logger.getLogger("main")->info("FIX Session Manager stopped");
}

std::shared_ptr<FIXSession> FIXSessionManager::getSession(const std::string& sessionId) {
    auto it = sessions_.find(sessionId);
    if (it != sessions_.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<order_manager::OrderManager> FIXSessionManager::getOrderManager(const std::string& sessionId) {
    auto it = orderManagers_.find(sessionId);
    if (it != orderManagers_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::string> FIXSessionManager::getSessionIds() const {
    std::vector<std::string> ids;
    ids.reserve(sessions_.size());
    
    for (const auto& [sessionId, _] : sessions_) {
        ids.push_back(sessionId);
    }
    
    return ids;
}

bool FIXSessionManager::isRunning() const {
    return running_;
}

void FIXSessionManager::runIoContext() {
    auto& logger = utils::Logger::getInstance();
    
    try {
        ioContext_.run();
    } catch (const std::exception& e) {
        logger.getLogger("main")->error(std::string("Exception in I/O thread: ") + e.what());
    }
}

}
}
