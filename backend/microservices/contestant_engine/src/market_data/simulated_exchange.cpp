#include "market_data/simulated_exchange.h"
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace MarketData {

SimulatedExchange::SimulatedExchange()
    : matchingEngine_(priceGenerator_),
      marketDataManager_(matchingEngine_),
      running_(false),
      updateInterval_(100) {
    
    matchingEngine_.setTradeCallback([this](const Trade& trade) {
        marketDataManager_.publishTrade(trade);
    });
}

SimulatedExchange::~SimulatedExchange() {
    stop();
}

void SimulatedExchange::start() {
    if (running_.exchange(true)) return;
    
    priceThread_ = std::make_unique<std::thread>([this]() {
        priceUpdateLoop();
    });
}

void SimulatedExchange::stop() {
    if (!running_.exchange(false)) return;
    
    if (priceThread_ && priceThread_->joinable()) {
        priceThread_->join();
    }
}

void SimulatedExchange::loadSymbols(const std::string& symbolsFile) {
    symbolManager_.loadFromJson(symbolsFile);
}

void SimulatedExchange::initializePrices() {
    std::ifstream file("config/symbols.json");
    if (!file.is_open()) return;
    
    json j;
    file >> j;
    
    if (j.contains("instruments")) {
        for (const auto& inst : j["instruments"]) {
            std::string symbol = inst.value("symbol", "");
            double basePrice = inst.value("basePrice", 100.0);
            double volatility = inst.value("volatility", 0.02);
            
            PriceConfig config(basePrice, volatility, 0.0, 1.0, 10000.0, 10.0);
            priceGenerator_.addSymbol(symbol, config);
        }
    }
}

void SimulatedExchange::submitOrder(const std::shared_ptr<Order>& order) {
    matchingEngine_.submitOrder(order);
}

void SimulatedExchange::cancelOrder(const std::string& orderId, const std::string& symbol) {
    matchingEngine_.cancelOrder(orderId, symbol);
}

void SimulatedExchange::handleMarketDataRequest(const std::string& sessionId, 
                                                const fix_gateway::fix_engine::FIXMessage& request) {
    marketDataManager_.handleMarketDataRequest(sessionId, request);
}

void SimulatedExchange::handleSessionDisconnect(const std::string& sessionId) {
    marketDataManager_.handleSessionDisconnect(sessionId);
}

void SimulatedExchange::setFillCallback(FillCallback callback) {
    matchingEngine_.setFillCallback(std::move(callback));
}

void SimulatedExchange::setMarketDataCallback(MarketDataCallback callback) {
    marketDataManager_.setMarketDataCallback(std::move(callback));
}

void SimulatedExchange::priceUpdateLoop() {
    while (running_) {
        updatePrices();
        publishQuotes();
        std::this_thread::sleep_for(updateInterval_);
    }
}

void SimulatedExchange::updatePrices() {
    auto symbols = symbolManager_.getTradableSymbols();
    
    for (const auto& symbol : symbols) {
        priceGenerator_.updatePrice(symbol);
    }
}

void SimulatedExchange::publishQuotes() {
    auto symbols = symbolManager_.getTradableSymbols();
    
    for (const auto& symbol : symbols) {
        double bidPrice = priceGenerator_.getBidPrice(symbol);
        double askPrice = priceGenerator_.getAskPrice(symbol);
        
        Quote quote(symbol, bidPrice, 100.0, askPrice, 100.0);
        marketDataManager_.publishQuoteUpdate(symbol, quote);
    }
}

}
