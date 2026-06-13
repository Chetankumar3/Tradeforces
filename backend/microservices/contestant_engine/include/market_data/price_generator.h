#pragma once

#include <string>
#include <random>
#include <map>
#include <mutex>

namespace MarketData {

struct PriceConfig {
    double basePrice;
    double volatility;
    double drift;
    double minPrice;
    double maxPrice;
    double spreadBps;

    PriceConfig(double base = 100.0, double vol = 0.02, double d = 0.0,
                double minP = 1.0, double maxP = 10000.0, double spread = 10.0)
        : basePrice(base), volatility(vol), drift(d), 
          minPrice(minP), maxPrice(maxP), spreadBps(spread) {}
};

class PriceGenerator {
public:
    PriceGenerator();

    void addSymbol(const std::string& symbol, const PriceConfig& config);
    void updatePrice(const std::string& symbol);
    
    double getMidPrice(const std::string& symbol) const;
    double getBidPrice(const std::string& symbol) const;
    double getAskPrice(const std::string& symbol) const;
    double getSpread(const std::string& symbol) const;
    
    void setLastTradePrice(const std::string& symbol, double price);

private:
    struct SymbolPrice {
        double currentPrice;
        double bidPrice;
        double askPrice;
        PriceConfig config;

        explicit SymbolPrice(const PriceConfig& cfg)
            : currentPrice(cfg.basePrice), config(cfg) {
            updateQuotes();
        }

        void updateQuotes() {
            double halfSpread = currentPrice * config.spreadBps / 20000.0;
            bidPrice = currentPrice - halfSpread;
            askPrice = currentPrice + halfSpread;
        }
    };

    std::map<std::string, SymbolPrice> prices_;
    mutable std::mutex mutex_;
    std::mt19937 rng_;
    std::normal_distribution<double> distribution_;
};

}
