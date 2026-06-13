#pragma once

#include "order_types.h"
#include <unordered_map>
#include <string>
#include <mutex>

namespace fix_gateway {
namespace order_manager {

struct Position {
    std::string symbol;
    std::string account;
    double quantity{0.0};
    double avgPrice{0.0};
    double realizedPnL{0.0};
    double unrealizedPnL{0.0};
    
    void updatePosition(Side side, double qty, double price);
    double getMarketValue(double currentPrice) const;
    void calculateUnrealizedPnL(double currentPrice);
};

class PositionTracker {
public:
    PositionTracker() = default;
    
    void updatePosition(const std::string& account, const std::string& symbol, 
                       Side side, double qty, double price);
    
    Position getPosition(const std::string& account, const std::string& symbol) const;
    std::unordered_map<std::string, Position> getPositionsByAccount(const std::string& account) const;
    
    double getTotalExposure(const std::string& account) const;
    double getSymbolExposure(const std::string& account, const std::string& symbol) const;
    
    void reset();
    void resetAccount(const std::string& account);

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unordered_map<std::string, Position>> positions_;
    
    std::string makeKey(const std::string& account, const std::string& symbol) const;
};

}
}
