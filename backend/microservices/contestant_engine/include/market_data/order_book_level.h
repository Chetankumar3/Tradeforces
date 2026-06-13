#pragma once

#include <map>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
#include <utility>
#include <string>
#include <functional>
#include "order_manager/order_types.h"

namespace MarketData {

using Order = fix_gateway::order_manager::Order;
using Side = fix_gateway::order_manager::Side;

struct PriceLevel {
    double price;
    double totalQuantity;
    std::deque<std::shared_ptr<Order>> orders;

    PriceLevel() : price(0.0), totalQuantity(0.0) {}
    explicit PriceLevel(double p) : price(p), totalQuantity(0.0) {}

    void addOrder(const std::shared_ptr<Order>& order);
    void removeOrder(const std::string& orderId);
    bool isEmpty() const { return orders.empty(); }
};

class OrderBookLevel {
public:
    explicit OrderBookLevel(const std::string& symbol);

    void addOrder(const std::shared_ptr<Order>& order);
    void removeOrder(const std::string& orderId, Side side);
    void modifyOrder(const std::string& orderId, double newQty, double newPrice);
    
    std::vector<std::pair<double, double>> getBidLevels(int depth = 5) const;
    std::vector<std::pair<double, double>> getAskLevels(int depth = 5) const;
    
    double getBestBid() const;
    double getBestAsk() const;
    double getMidPrice() const;
    double getSpread() const;
    
    size_t getBidDepth() const;
    size_t getAskDepth() const;
    
    std::shared_ptr<Order> getBestBidOrder();
    std::shared_ptr<Order> getBestAskOrder();

private:
    std::string symbol_;
    std::map<double, PriceLevel, std::greater<>> bidLevels_;
    std::map<double, PriceLevel, std::less<>> askLevels_;
    mutable std::mutex mutex_;

    void addBuyOrder(const std::shared_ptr<Order>& order);
    void addSellOrder(const std::shared_ptr<Order>& order);
};

}
