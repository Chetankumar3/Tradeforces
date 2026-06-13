#pragma once

#include "order_types.h"
#include <unordered_map>
#include <map>
#include <vector>
#include <mutex>
#include <memory>
#include <optional>

namespace fix_gateway {
namespace order_manager {

class OrderBook {
public:
    OrderBook() = default;
    
    void addOrder(std::shared_ptr<Order> order);
    bool removeOrder(const std::string& orderId);
    bool updateOrder(std::shared_ptr<Order> order);
    
    std::shared_ptr<Order> getOrder(const std::string& orderId) const;
    std::shared_ptr<Order> getOrderByClOrdId(const std::string& clOrdId) const;
    
    std::vector<std::shared_ptr<Order>> getOrdersBySymbol(const std::string& symbol) const;
    std::vector<std::shared_ptr<Order>> getActiveOrders() const;
    std::vector<std::shared_ptr<Order>> getOrdersByAccount(const std::string& account) const;
    
    size_t getOrderCount() const;
    size_t getActiveOrderCount() const;
    
    void clearOrders();

private:
    mutable std::mutex mutex_;
    
    std::unordered_map<std::string, std::shared_ptr<Order>> ordersByOrderId_;
    std::unordered_map<std::string, std::shared_ptr<Order>> ordersByClOrdId_;
    std::unordered_map<std::string, std::vector<std::shared_ptr<Order>>> ordersBySymbol_;
    std::unordered_map<std::string, std::vector<std::shared_ptr<Order>>> ordersByAccount_;
    
    void indexOrder(std::shared_ptr<Order> order);
    void deindexOrder(const std::string& orderId);
};

}
}
