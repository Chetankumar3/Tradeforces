#pragma once

#include <map>
#include <set>
#include <mutex>
#include <functional>
#include <vector>
#include <string>
#include "market_data/market_data_types.h"
#include "market_data/matching_engine.h"
#include "fix_engine/fix_message.h"

namespace MarketData {

using FIXMessage = fix_gateway::fix_engine::FIXMessage;
using MarketDataCallback = std::function<void(const std::string& sessionId, const FIXMessage&)>;

class MarketDataManager {
public:
    explicit MarketDataManager(MatchingEngine& matchingEngine);

    void handleMarketDataRequest(const std::string& sessionId, const FIXMessage& request);
    void handleUnsubscribe(const std::string& sessionId, const std::string& symbol);
    void handleSessionDisconnect(const std::string& sessionId);
    
    void publishTrade(const Trade& trade);
    void publishQuoteUpdate(const std::string& symbol, const Quote& quote);
    
    void setMarketDataCallback(MarketDataCallback callback);
    
    std::vector<std::string> getSubscribedSymbols(const std::string& sessionId) const;
    size_t getSubscriptionCount() const;

private:
    void sendSnapshot(const std::string& sessionId, const std::string& symbol, int depth);
    void sendIncrementalRefresh(const std::string& symbol, const MarketDataIncrementalRefresh& update);
    
    FIXMessage createSnapshotMessage(const std::string& symbol, 
                                     const std::vector<std::pair<double, double>>& bids,
                                     const std::vector<std::pair<double, double>>& asks);
    FIXMessage createIncrementalRefreshMessage(const MarketDataIncrementalRefresh& update);
    
    struct Subscription {
        std::string sessionId;
        std::string symbol;
        SubscriptionRequestType type;
        int depth;
    };

    std::map<std::string, std::vector<Subscription>> symbolSubscriptions_;
    std::map<std::string, std::set<std::string>> sessionSubscriptions_;
    
    MatchingEngine& matchingEngine_;
    MarketDataCallback marketDataCallback_;
    
    mutable std::mutex mutex_;
};

}
