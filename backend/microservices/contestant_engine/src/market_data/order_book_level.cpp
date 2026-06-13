#include "market_data/order_book_level.h"
#include <algorithm>

namespace MarketData {

void PriceLevel::addOrder(const std::shared_ptr<Order>& order) {
    orders.push_back(order);
    totalQuantity += (order->orderQty - order->cumQty);
}

void PriceLevel::removeOrder(const std::string& orderId) {
    auto it = std::find_if(orders.begin(), orders.end(),
        [&orderId](const auto& order) { return order->orderId == orderId; });
    
    if (it != orders.end()) {
        totalQuantity -= ((*it)->orderQty - (*it)->cumQty);
        orders.erase(it);
    }
}

OrderBookLevel::OrderBookLevel(const std::string& symbol) : symbol_(symbol) {}

void OrderBookLevel::addOrder(const std::shared_ptr<Order>& order) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (order->side == Side::BUY) {
        addBuyOrder(order);
    } else {
        addSellOrder(order);
    }
}

void OrderBookLevel::addBuyOrder(const std::shared_ptr<Order>& order) {
    auto& level = bidLevels_[order->price];
    if (level.price == 0.0) {
        level.price = order->price;
    }
    level.addOrder(order);
}

void OrderBookLevel::addSellOrder(const std::shared_ptr<Order>& order) {
    auto& level = askLevels_[order->price];
    if (level.price == 0.0) {
        level.price = order->price;
    }
    level.addOrder(order);
}

void OrderBookLevel::removeOrder(const std::string& orderId, Side side) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (side == Side::BUY) {
        for (auto it = bidLevels_.begin(); it != bidLevels_.end();) {
            it->second.removeOrder(orderId);
            if (it->second.isEmpty()) {
                it = bidLevels_.erase(it);
            } else {
                ++it;
            }
        }
    } else {
        for (auto it = askLevels_.begin(); it != askLevels_.end();) {
            it->second.removeOrder(orderId);
            if (it->second.isEmpty()) {
                it = askLevels_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void OrderBookLevel::modifyOrder(const std::string& orderId, double newQty, double newPrice) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& [price, level] : bidLevels_) {
        auto it = std::find_if(level.orders.begin(), level.orders.end(),
            [&orderId](const auto& order) { return order->orderId == orderId; });
        if (it != level.orders.end()) {
            (*it)->orderQty = newQty;
            (*it)->price = newPrice;
            return;
        }
    }
    
    for (auto& [price, level] : askLevels_) {
        auto it = std::find_if(level.orders.begin(), level.orders.end(),
            [&orderId](const auto& order) { return order->orderId == orderId; });
        if (it != level.orders.end()) {
            (*it)->orderQty = newQty;
            (*it)->price = newPrice;
            return;
        }
    }
}

std::vector<std::pair<double, double>> OrderBookLevel::getBidLevels(int depth) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<double, double>> result;
    
    int count = 0;
    for (const auto& [price, level] : bidLevels_) {
        if (count++ >= depth) break;
        result.emplace_back(price, level.totalQuantity);
    }
    return result;
}

std::vector<std::pair<double, double>> OrderBookLevel::getAskLevels(int depth) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<double, double>> result;
    
    int count = 0;
    for (const auto& [price, level] : askLevels_) {
        if (count++ >= depth) break;
        result.emplace_back(price, level.totalQuantity);
    }
    return result;
}

double OrderBookLevel::getBestBid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bidLevels_.empty() ? 0.0 : bidLevels_.begin()->first;
}

double OrderBookLevel::getBestAsk() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return askLevels_.empty() ? 0.0 : askLevels_.begin()->first;
}

double OrderBookLevel::getMidPrice() const {
    double bid = getBestBid();
    double ask = getBestAsk();
    return (bid > 0 && ask > 0) ? (bid + ask) / 2.0 : 0.0;
}

double OrderBookLevel::getSpread() const {
    double bid = getBestBid();
    double ask = getBestAsk();
    return (bid > 0 && ask > 0) ? (ask - bid) : 0.0;
}

size_t OrderBookLevel::getBidDepth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bidLevels_.size();
}

size_t OrderBookLevel::getAskDepth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return askLevels_.size();
}

std::shared_ptr<Order> OrderBookLevel::getBestBidOrder() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (bidLevels_.empty() || bidLevels_.begin()->second.orders.empty()) {
        return std::shared_ptr<Order>();
    }
    return bidLevels_.begin()->second.orders.front();
}

std::shared_ptr<Order> OrderBookLevel::getBestAskOrder() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (askLevels_.empty() || askLevels_.begin()->second.orders.empty()) {
        return std::shared_ptr<Order>();
    }
    return askLevels_.begin()->second.orders.front();
}

}
