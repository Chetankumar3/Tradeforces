#include "market_data/price_generator.h"
#include <algorithm>
#include <cmath>

namespace MarketData {

PriceGenerator::PriceGenerator() 
    : rng_(std::random_device{}()), distribution_(0.0, 1.0) {}

void PriceGenerator::addSymbol(const std::string& symbol, const PriceConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    prices_.emplace(symbol, SymbolPrice(config));
}

void PriceGenerator::updatePrice(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = prices_.find(symbol);
    if (it == prices_.end()) return;
    
    auto& sp = it->second;
    
    double randomChange = distribution_(rng_) * sp.config.volatility;
    double priceChange = sp.currentPrice * (sp.config.drift + randomChange);
    
    sp.currentPrice += priceChange;
    sp.currentPrice = std::clamp(sp.currentPrice, sp.config.minPrice, sp.config.maxPrice);
    
    sp.updateQuotes();
}

double PriceGenerator::getMidPrice(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = prices_.find(symbol);
    return it != prices_.end() ? it->second.currentPrice : 0.0;
}

double PriceGenerator::getBidPrice(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = prices_.find(symbol);
    return it != prices_.end() ? it->second.bidPrice : 0.0;
}

double PriceGenerator::getAskPrice(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = prices_.find(symbol);
    return it != prices_.end() ? it->second.askPrice : 0.0;
}

double PriceGenerator::getSpread(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = prices_.find(symbol);
    return it != prices_.end() ? (it->second.askPrice - it->second.bidPrice) : 0.0;
}

void PriceGenerator::setLastTradePrice(const std::string& symbol, double price) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = prices_.find(symbol);
    if (it != prices_.end()) {
        it->second.currentPrice = price;
        it->second.updateQuotes();
    }
}

}
