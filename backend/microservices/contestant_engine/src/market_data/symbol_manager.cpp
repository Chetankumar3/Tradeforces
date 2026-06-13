#include "market_data/symbol_manager.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <cmath>
#include <algorithm>

using json = nlohmann::json;

namespace MarketData {

void SymbolManager::loadFromJson(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::ifstream file(filename);
    if (!file.is_open()) return;
    
    json j;
    file >> j;
    
    if (j.contains("instruments")) {
        for (const auto& inst : j["instruments"]) {
            InstrumentDetails details;
            details.symbol = inst.value("symbol", "");
            details.instrumentType = inst.value("type", "STOCK");
            details.tickSize = inst.value("tickSize", 0.01);
            details.lotSize = inst.value("lotSize", 1.0);
            details.minPrice = inst.value("minPrice", 0.01);
            details.maxPrice = inst.value("maxPrice", 1000000.0);
            details.tradingHours = inst.value("tradingHours", "09:30-16:00");
            details.isActive = inst.value("isActive", true);
            
            if (!details.symbol.empty()) {
                instruments_[details.symbol] = details;
            }
        }
    }
}

bool SymbolManager::addSymbol(const std::string& symbol, const InstrumentDetails& details) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (instruments_.count(symbol)) {
        return false;
    }
    
    instruments_[symbol] = details;
    return true;
}

bool SymbolManager::removeSymbol(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mutex_);
    return instruments_.erase(symbol) > 0;
}

bool SymbolManager::isValidSymbol(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return instruments_.count(symbol) > 0;
}

bool SymbolManager::isTradable(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = instruments_.find(symbol);
    return it != instruments_.end() && it->second.isActive;
}

InstrumentDetails SymbolManager::getInstrumentDetails(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = instruments_.find(symbol);
    return it != instruments_.end() ? it->second : InstrumentDetails();
}

std::vector<std::string> SymbolManager::getAllSymbols() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> result;
    result.reserve(instruments_.size());
    
    for (const auto& [symbol, _] : instruments_) {
        result.push_back(symbol);
    }
    
    return result;
}

std::vector<std::string> SymbolManager::getTradableSymbols() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> result;
    
    for (const auto& [symbol, details] : instruments_) {
        if (details.isActive) {
            result.push_back(symbol);
        }
    }
    
    return result;
}

double SymbolManager::roundToTickSize(const std::string& symbol, double price) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = instruments_.find(symbol);
    if (it == instruments_.end()) return price;
    
    double tickSize = it->second.tickSize;
    return std::round(price / tickSize) * tickSize;
}

double SymbolManager::roundToLotSize(const std::string& symbol, double quantity) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = instruments_.find(symbol);
    if (it == instruments_.end()) return quantity;
    
    double lotSize = it->second.lotSize;
    return std::round(quantity / lotSize) * lotSize;
}

bool SymbolManager::isWithinPriceLimits(const std::string& symbol, double price) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = instruments_.find(symbol);
    if (it == instruments_.end()) return false;
    
    return price >= it->second.minPrice && price <= it->second.maxPrice;
}

}
