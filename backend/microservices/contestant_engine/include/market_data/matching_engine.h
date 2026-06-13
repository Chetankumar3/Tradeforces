#pragma once

#include <memory>
#include <functional>
#include <mutex>
#include <map>
#include <vector>
#include <deque>
#include <string>
#include <chrono>
#include "order_manager/order_types.h"
#include "market_data/market_data_types.h"
#include "market_data/price_generator.h"

namespace MarketData {

// Forward declaration
class OrderBookLevel;

using Order = fix_gateway::order_manager::Order;
using OrderType = fix_gateway::order_manager::OrderType;
using OrderState = fix_gateway::order_manager::OrderState;
using Side = fix_gateway::order_manager::Side;

struct Fill {
    std::string buyOrderId;
    std::string sellOrderId;
    std::string aggressorOrderId;
    std::string symbol;
    double price;
    double quantity;
    std::chrono::system_clock::time_point timestamp;

    Fill(const std::string& buyId, const std::string& sellId,
         const std::string& sym, double p, double q,
         const std::string& aggressorId = "")
        : buyOrderId(buyId), sellOrderId(sellId), aggressorOrderId(aggressorId),
          symbol(sym), price(p), quantity(q),
          timestamp(std::chrono::system_clock::now()) {}
};

using FillCallback = std::function<void(const Fill&)>;
using TradeCallback = std::function<void(const Trade&)>;

class MatchingEngine {
public:
    explicit MatchingEngine(PriceGenerator& priceGen);

    void submitOrder(const std::shared_ptr<Order>& order);
    void cancelOrder(const std::string& orderId, const std::string& symbol);
    
    void setFillCallback(FillCallback callback);
    void setTradeCallback(TradeCallback callback);
    
    std::shared_ptr<OrderBookLevel> getOrderBook(const std::string& symbol);
    std::vector<Fill> getRecentFills(const std::string& symbol, size_t limit = 100) const;

private:
    void matchOrder(const std::shared_ptr<Order>& order);
    void matchMarketOrder(const std::shared_ptr<Order>& order);
    void matchLimitOrder(const std::shared_ptr<Order>& order);
    void checkStopOrders(const std::string& symbol, double lastPrice);
    
    void executeFill(const std::shared_ptr<Order>& aggressorOrder,
                     const std::shared_ptr<Order>& restingOrder,
                     double fillPrice, double fillQty);
    
    void addToBook(const std::shared_ptr<Order>& order);
    
    std::string generateTradeId();

    std::map<std::string, std::shared_ptr<OrderBookLevel>> books_;
    std::map<std::string, std::vector<std::shared_ptr<Order>>> stopOrders_;
    std::map<std::string, std::deque<Fill>> recentFills_;
    
    PriceGenerator& priceGen_;
    FillCallback fillCallback_;
    TradeCallback tradeCallback_;
    
    mutable std::mutex mutex_;
    uint64_t tradeCounter_;
};

}
