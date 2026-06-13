#include "order_manager/position_tracker.h"
#include <cmath>

namespace fix_gateway {
namespace order_manager {

void Position::updatePosition(Side side, double qty, double price) {
    double sideMultiplier = (side == Side::BUY) ? 1.0 : -1.0;
    double tradedValue = qty * price;
    
    if ((quantity >= 0 && side == Side::BUY) || (quantity < 0 && side == Side::SELL)) {
        double totalCost = std::abs(quantity) * avgPrice + tradedValue;
        quantity += sideMultiplier * qty;
        avgPrice = std::abs(quantity) > 0 ? totalCost / std::abs(quantity) : 0.0;
    } else {
        double closedQty = std::min(qty, std::abs(quantity));
        realizedPnL += closedQty * (price - avgPrice) * (quantity >= 0 ? 1.0 : -1.0);
        
        quantity += sideMultiplier * qty;
        
        if (std::abs(quantity) < 1e-6) {
            quantity = 0.0;
            avgPrice = 0.0;
        } else if (qty > closedQty) {
            avgPrice = price;
        }
    }
}

double Position::getMarketValue(double currentPrice) const {
    return quantity * currentPrice;
}

void Position::calculateUnrealizedPnL(double currentPrice) {
    if (std::abs(quantity) > 1e-6) {
        unrealizedPnL = (currentPrice - avgPrice) * quantity;
    } else {
        unrealizedPnL = 0.0;
    }
}

void PositionTracker::updatePosition(const std::string& account, const std::string& symbol,
                                    Side side, double qty, double price) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& accountPositions = positions_[account];
    auto& position = accountPositions[symbol];
    
    if (position.symbol.empty()) {
        position.symbol = symbol;
        position.account = account;
    }
    
    position.updatePosition(side, qty, price);
}

Position PositionTracker::getPosition(const std::string& account, const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto accountIt = positions_.find(account);
    if (accountIt == positions_.end()) {
        Position emptyPosition;
        emptyPosition.symbol = symbol;
        emptyPosition.account = account;
        return emptyPosition;
    }
    
    auto posIt = accountIt->second.find(symbol);
    if (posIt == accountIt->second.end()) {
        Position emptyPosition;
        emptyPosition.symbol = symbol;
        emptyPosition.account = account;
        return emptyPosition;
    }
    
    return posIt->second;
}

std::unordered_map<std::string, Position> PositionTracker::getPositionsByAccount(
    const std::string& account) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = positions_.find(account);
    return it != positions_.end() ? it->second : std::unordered_map<std::string, Position>();
}

double PositionTracker::getTotalExposure(const std::string& account) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = positions_.find(account);
    if (it == positions_.end()) {
        return 0.0;
    }
    
    double totalExposure = 0.0;
    for (const auto& [symbol, position] : it->second) {
        totalExposure += std::abs(position.quantity * position.avgPrice);
    }
    
    return totalExposure;
}

double PositionTracker::getSymbolExposure(const std::string& account, 
                                         const std::string& symbol) const {
    auto position = getPosition(account, symbol);
    return std::abs(position.quantity * position.avgPrice);
}

void PositionTracker::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    positions_.clear();
}

void PositionTracker::resetAccount(const std::string& account) {
    std::lock_guard<std::mutex> lock(mutex_);
    positions_.erase(account);
}

std::string PositionTracker::makeKey(const std::string& account, const std::string& symbol) const {
    return account + "|" + symbol;
}

}
}
