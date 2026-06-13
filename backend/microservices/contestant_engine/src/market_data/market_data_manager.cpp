#include "market_data/market_data_manager.h"
#include "market_data/order_book_level.h"
#include <algorithm>

namespace MarketData {

MarketDataManager::MarketDataManager(MatchingEngine& matchingEngine)
    : matchingEngine_(matchingEngine) {}

void MarketDataManager::handleMarketDataRequest(const std::string& sessionId, 
                                                 const fix_gateway::fix_engine::FIXMessage& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto reqTypeOpt = request.getField(263);
    int reqType = reqTypeOpt ? std::stoi(*reqTypeOpt) : 0;
    
    auto symbolOpt = request.getField(55);
    if (!symbolOpt) return;
    std::string symbol = *symbolOpt;
    
    auto depthOpt = request.getField(264);
    int depth = depthOpt ? std::stoi(*depthOpt) : 5;
    
    SubscriptionRequestType subType = static_cast<SubscriptionRequestType>(reqType);
    
    if (subType == SubscriptionRequestType::UNSUBSCRIBE) {
        handleUnsubscribe(sessionId, symbol);
        return;
    }
    
    Subscription sub{sessionId, symbol, subType, depth};
    symbolSubscriptions_[symbol].push_back(sub);
    sessionSubscriptions_[sessionId].insert(symbol);
    
    sendSnapshot(sessionId, symbol, depth);
}

void MarketDataManager::handleUnsubscribe(const std::string& sessionId, const std::string& symbol) {
    auto it = symbolSubscriptions_.find(symbol);
    if (it != symbolSubscriptions_.end()) {
        auto& subs = it->second;
        subs.erase(
            std::remove_if(subs.begin(), subs.end(),
                [&sessionId](const Subscription& sub) { return sub.sessionId == sessionId; }),
            subs.end());
        
        if (subs.empty()) {
            symbolSubscriptions_.erase(it);
        }
    }
    
    auto sesIt = sessionSubscriptions_.find(sessionId);
    if (sesIt != sessionSubscriptions_.end()) {
        sesIt->second.erase(symbol);
        if (sesIt->second.empty()) {
            sessionSubscriptions_.erase(sesIt);
        }
    }
}

void MarketDataManager::handleSessionDisconnect(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessionSubscriptions_.find(sessionId);
    if (it == sessionSubscriptions_.end()) return;
    
    for (const auto& symbol : it->second) {
        auto symIt = symbolSubscriptions_.find(symbol);
        if (symIt != symbolSubscriptions_.end()) {
            auto& subs = symIt->second;
            subs.erase(
                std::remove_if(subs.begin(), subs.end(),
                    [&sessionId](const Subscription& sub) { return sub.sessionId == sessionId; }),
                subs.end());
            
            if (subs.empty()) {
                symbolSubscriptions_.erase(symIt);
            }
        }
    }
    
    sessionSubscriptions_.erase(it);
}

void MarketDataManager::sendSnapshot(const std::string& sessionId, 
                                     const std::string& symbol, int depth) {
    auto book = matchingEngine_.getOrderBook(symbol);
    if (!book) return;
    
    auto bids = book->getBidLevels(depth);
    auto asks = book->getAskLevels(depth);
    
    auto message = createSnapshotMessage(symbol, bids, asks);
    
    if (marketDataCallback_) {
        marketDataCallback_(sessionId, message);
    }
}

void MarketDataManager::publishTrade(const Trade& trade) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = symbolSubscriptions_.find(trade.symbol);
    if (it == symbolSubscriptions_.end()) return;
    
    MDEntry entry(MDEntryType::TRADE, trade.price, trade.quantity);
    MarketDataIncrementalRefresh update(trade.symbol, UpdateAction::NEW, entry);
    
    for (const auto& sub : it->second) {
        if (sub.type == SubscriptionRequestType::SNAPSHOT_PLUS_UPDATES) {
            sendIncrementalRefresh(trade.symbol, update);
        }
    }
}

void MarketDataManager::publishQuoteUpdate(const std::string& symbol, const Quote& quote) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = symbolSubscriptions_.find(symbol);
    if (it == symbolSubscriptions_.end()) return;
    
    for (const auto& sub : it->second) {
        if (sub.type == SubscriptionRequestType::SNAPSHOT_PLUS_UPDATES) {
            MDEntry bidEntry(MDEntryType::BID, quote.bidPrice, quote.bidSize);
            MarketDataIncrementalRefresh bidUpdate(symbol, UpdateAction::CHANGE, bidEntry);
            sendIncrementalRefresh(symbol, bidUpdate);
            
            MDEntry askEntry(MDEntryType::OFFER, quote.askPrice, quote.askSize);
            MarketDataIncrementalRefresh askUpdate(symbol, UpdateAction::CHANGE, askEntry);
            sendIncrementalRefresh(symbol, askUpdate);
        }
    }
}

void MarketDataManager::sendIncrementalRefresh(const std::string& symbol, 
                                               const MarketDataIncrementalRefresh& update) {
    auto message = createIncrementalRefreshMessage(update);
    
    auto it = symbolSubscriptions_.find(symbol);
    if (it == symbolSubscriptions_.end()) return;
    
    if (marketDataCallback_) {
        for (const auto& sub : it->second) {
            if (sub.type == SubscriptionRequestType::SNAPSHOT_PLUS_UPDATES) {
                marketDataCallback_(sub.sessionId, message);
            }
        }
    }
}

fix_gateway::fix_engine::FIXMessage MarketDataManager::createSnapshotMessage(
    const std::string& symbol,
    const std::vector<std::pair<double, double>>& bids,
    const std::vector<std::pair<double, double>>& asks) {
    
    fix_gateway::fix_engine::FIXMessage msg;
    msg.setMsgType(fix_gateway::fix_engine::MsgType::MARKET_DATA_SNAPSHOT);
    msg.setField(55, symbol);
    msg.setField(268, std::to_string(bids.size() + asks.size()));
    
    int entryCount = 0;
    for (size_t i = 0; i < bids.size(); ++i) {
        msg.setField(269 + entryCount * 1000, "0");
        msg.setField(270 + entryCount * 1000, std::to_string(bids[i].first));
        msg.setField(271 + entryCount * 1000, std::to_string(bids[i].second));
        ++entryCount;
    }
    
    for (size_t i = 0; i < asks.size(); ++i) {
        msg.setField(269 + entryCount * 1000, "1");
        msg.setField(270 + entryCount * 1000, std::to_string(asks[i].first));
        msg.setField(271 + entryCount * 1000, std::to_string(asks[i].second));
        ++entryCount;
    }
    
    return msg;
}

fix_gateway::fix_engine::FIXMessage MarketDataManager::createIncrementalRefreshMessage(
    const MarketDataIncrementalRefresh& update) {
    
    fix_gateway::fix_engine::FIXMessage msg;
    msg.setMsgType(fix_gateway::fix_engine::MsgType::MARKET_DATA_INCREMENTAL_REFRESH);
    msg.setField(55, update.symbol);
    msg.setField(268, "1");
    
    msg.setField(279, std::to_string(static_cast<int>(update.action)));
    msg.setField(269, std::to_string(static_cast<int>(update.entry.type)));
    msg.setField(270, std::to_string(update.entry.price));
    msg.setField(271, std::to_string(update.entry.size));
    
    return msg;
}

void MarketDataManager::setMarketDataCallback(MarketDataCallback callback) {
    marketDataCallback_ = std::move(callback);
}

std::vector<std::string> MarketDataManager::getSubscribedSymbols(const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessionSubscriptions_.find(sessionId);
    if (it == sessionSubscriptions_.end()) return {};
    
    return std::vector<std::string>(it->second.begin(), it->second.end());
}

size_t MarketDataManager::getSubscriptionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessionSubscriptions_.size();
}

}
