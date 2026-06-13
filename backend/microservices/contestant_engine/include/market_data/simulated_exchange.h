#pragma once

#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <string>
#include "market_data/matching_engine.h"
#include "market_data/market_data_manager.h"
#include "market_data/price_generator.h"
#include "market_data/symbol_manager.h"
#include "order_manager/order_types.h"
#include "fix_engine/fix_message.h"

namespace MarketData {

using Order = fix_gateway::order_manager::Order;
using FIXMessage = fix_gateway::fix_engine::FIXMessage;

class SimulatedExchange {
public:
    SimulatedExchange();
    ~SimulatedExchange();

    void start();
    void stop();
    
    void loadSymbols(const std::string& symbolsFile);
    void initializePrices();
    
    void submitOrder(const std::shared_ptr<Order>& order);
    void cancelOrder(const std::string& orderId, const std::string& symbol);
    
    void handleMarketDataRequest(const std::string& sessionId, const FIXMessage& request);
    void handleSessionDisconnect(const std::string& sessionId);
    
    void setFillCallback(FillCallback callback);
    void setMarketDataCallback(MarketDataCallback callback);
    
    SymbolManager& getSymbolManager() { return symbolManager_; }
    MatchingEngine& getMatchingEngine() { return matchingEngine_; }
    PriceGenerator& getPriceGenerator() { return priceGenerator_; }
    MarketDataManager& getMarketDataManager() { return marketDataManager_; }

private:
    void priceUpdateLoop();
    void updatePrices();
    void publishQuotes();

    SymbolManager symbolManager_;
    PriceGenerator priceGenerator_;
    MatchingEngine matchingEngine_;
    MarketDataManager marketDataManager_;
    
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> priceThread_;
    std::chrono::milliseconds updateInterval_;
};

}
