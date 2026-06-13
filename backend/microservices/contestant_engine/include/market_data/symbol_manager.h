#pragma once

#include <string>
#include <map>
#include <mutex>
#include <vector>

namespace MarketData {

struct InstrumentDetails {
    std::string symbol;
    std::string instrumentType;
    double tickSize;
    double lotSize;
    double minPrice;
    double maxPrice;
    std::string tradingHours;
    bool isActive;

    InstrumentDetails()
        : tickSize(0.01), lotSize(1.0), minPrice(0.01), 
          maxPrice(1000000.0), isActive(true) {}
};

class SymbolManager {
public:
    SymbolManager() = default;

    void loadFromJson(const std::string& filename);
    
    bool addSymbol(const std::string& symbol, const InstrumentDetails& details);
    bool removeSymbol(const std::string& symbol);
    
    bool isValidSymbol(const std::string& symbol) const;
    bool isTradable(const std::string& symbol) const;
    
    InstrumentDetails getInstrumentDetails(const std::string& symbol) const;
    std::vector<std::string> getAllSymbols() const;
    std::vector<std::string> getTradableSymbols() const;
    
    double roundToTickSize(const std::string& symbol, double price) const;
    double roundToLotSize(const std::string& symbol, double quantity) const;
    
    bool isWithinPriceLimits(const std::string& symbol, double price) const;

private:
    std::map<std::string, InstrumentDetails> instruments_;
    mutable std::mutex mutex_;
};

}
