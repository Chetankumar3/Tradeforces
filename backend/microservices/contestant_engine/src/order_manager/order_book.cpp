#include "order_manager/order_book.h"
#include <algorithm>

namespace fix_gateway {
namespace order_manager {

void OrderBook::addOrder(std::shared_ptr<Order> order) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    ordersByOrderId_[order->orderId] = order;
    indexOrder(order);
}

bool OrderBook::removeOrder(const std::string& orderId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = ordersByOrderId_.find(orderId);
    if (it == ordersByOrderId_.end()) {
        return false;
    }
    
    deindexOrder(orderId);
    ordersByOrderId_.erase(it);
    
    return true;
}

bool OrderBook::updateOrder(std::shared_ptr<Order> order) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = ordersByOrderId_.find(order->orderId);
    if (it == ordersByOrderId_.end()) {
        return false;
    }
    
    deindexOrder(order->orderId);
    ordersByOrderId_[order->orderId] = order;
    indexOrder(order);
    
    return true;
}

std::shared_ptr<Order> OrderBook::getOrder(const std::string& orderId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = ordersByOrderId_.find(orderId);
    return it != ordersByOrderId_.end() ? it->second : nullptr;
}

std::shared_ptr<Order> OrderBook::getOrderByClOrdId(const std::string& clOrdId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = ordersByClOrdId_.find(clOrdId);
    return it != ordersByClOrdId_.end() ? it->second : nullptr;
}

std::vector<std::shared_ptr<Order>> OrderBook::getOrdersBySymbol(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = ordersBySymbol_.find(symbol);
    return it != ordersBySymbol_.end() ? it->second : std::vector<std::shared_ptr<Order>>();
}

std::vector<std::shared_ptr<Order>> OrderBook::getActiveOrders() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::shared_ptr<Order>> activeOrders;
    for (const auto& [orderId, order] : ordersByOrderId_) {
        if (order->isActive()) {
            activeOrders.push_back(order);
        }
    }
    
    return activeOrders;
}

std::vector<std::shared_ptr<Order>> OrderBook::getOrdersByAccount(const std::string& account) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = ordersByAccount_.find(account);
    return it != ordersByAccount_.end() ? it->second : std::vector<std::shared_ptr<Order>>();
}

size_t OrderBook::getOrderCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ordersByOrderId_.size();
}

size_t OrderBook::getActiveOrderCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    return std::count_if(ordersByOrderId_.begin(), ordersByOrderId_.end(),
        [](const auto& pair) { return pair.second->isActive(); });
}

void OrderBook::clearOrders() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    ordersByOrderId_.clear();
    ordersByClOrdId_.clear();
    ordersBySymbol_.clear();
    ordersByAccount_.clear();
}

void OrderBook::indexOrder(std::shared_ptr<Order> order) {
    ordersByClOrdId_[order->clOrdId] = order;
    ordersBySymbol_[order->symbol].push_back(order);
    ordersByAccount_[order->account].push_back(order);
}

void OrderBook::deindexOrder(const std::string& orderId) {
    auto orderIt = ordersByOrderId_.find(orderId);
    if (orderIt == ordersByOrderId_.end()) {
        return;
    }
    
    auto order = orderIt->second;
    
    ordersByClOrdId_.erase(order->clOrdId);
    
    auto& symbolOrders = ordersBySymbol_[order->symbol];
    symbolOrders.erase(std::remove_if(symbolOrders.begin(), symbolOrders.end(),
        [&orderId](const auto& o) { return o->orderId == orderId; }), symbolOrders.end());
    
    auto& accountOrders = ordersByAccount_[order->account];
    accountOrders.erase(std::remove_if(accountOrders.begin(), accountOrders.end(),
        [&orderId](const auto& o) { return o->orderId == orderId; }), accountOrders.end());
}

}
}
