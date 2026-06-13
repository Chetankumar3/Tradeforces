#pragma once

#include <string>
#include <vector>
#include <chrono>

namespace MarketData {

enum class MDEntryType {
    BID = 0,
    OFFER = 1,
    TRADE = 2,
    OPENING_PRICE = 4,
    CLOSING_PRICE = 5,
    HIGH_PRICE = 7,
    LOW_PRICE = 8,
    VWAP = 9
};

enum class SubscriptionRequestType {
    SNAPSHOT = 0,
    SNAPSHOT_PLUS_UPDATES = 1,
    UNSUBSCRIBE = 2
};

enum class UpdateAction {
    NEW = 0,
    CHANGE = 1,
    DELETE = 2
};

struct MDEntry {
    MDEntryType type;
    double price;
    double size;
    int level;
    std::chrono::system_clock::time_point timestamp;

    MDEntry(MDEntryType t, double p, double s, int l = 0)
        : type(t), price(p), size(s), level(l),
          timestamp(std::chrono::system_clock::now()) {}
};

struct MarketDataSnapshot {
    std::string symbol;
    std::vector<MDEntry> entries;
    std::chrono::system_clock::time_point timestamp;

    explicit MarketDataSnapshot(const std::string& sym)
        : symbol(sym), timestamp(std::chrono::system_clock::now()) {}
};

struct MarketDataIncrementalRefresh {
    std::string symbol;
    UpdateAction action;
    MDEntry entry;

    MarketDataIncrementalRefresh(const std::string& sym, UpdateAction act, const MDEntry& e)
        : symbol(sym), action(act), entry(e) {}
};

struct MarketDataSubscription {
    std::string sessionId;
    std::string symbol;
    SubscriptionRequestType type;
    int depth;
    
    MarketDataSubscription(const std::string& sid, const std::string& sym, 
                          SubscriptionRequestType t, int d = 5)
        : sessionId(sid), symbol(sym), type(t), depth(d) {}
};

struct Quote {
    std::string symbol;
    double bidPrice;
    double bidSize;
    double askPrice;
    double askSize;
    std::chrono::system_clock::time_point timestamp;

    Quote() : bidPrice(0.0), bidSize(0.0), askPrice(0.0), askSize(0.0),
              timestamp(std::chrono::system_clock::now()) {}
              
    Quote(const std::string& sym, double bp, double bs, double ap, double as)
        : symbol(sym), bidPrice(bp), bidSize(bs), askPrice(ap), askSize(as),
          timestamp(std::chrono::system_clock::now()) {}
};

struct Trade {
    std::string symbol;
    double price;
    double quantity;
    std::chrono::system_clock::time_point timestamp;
    std::string tradeId;

    Trade(const std::string& sym, double p, double q, const std::string& tid)
        : symbol(sym), price(p), quantity(q), 
          timestamp(std::chrono::system_clock::now()), tradeId(tid) {}
};

}
