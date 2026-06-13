#include "market_data/matching_engine.h"
#include "market_data/order_book_level.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace MarketData {

MatchingEngine::MatchingEngine(PriceGenerator& priceGen)
    : priceGen_(priceGen), tradeCounter_(0) {}

void MatchingEngine::submitOrder(const std::shared_ptr<Order>& order) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!books_.count(order->symbol)) {
        books_[order->symbol] = std::make_shared<OrderBookLevel>(order->symbol);
    }
    
    if (order->orderType == OrderType::STOP || 
        order->orderType == OrderType::STOP_LIMIT) {
        stopOrders_[order->symbol].push_back(order);
        return;
    }
    
    matchOrder(order);
}

void MatchingEngine::matchOrder(const std::shared_ptr<Order>& order) {
    if (order->orderType == OrderType::MARKET) {
        matchMarketOrder(order);
    } else {
        matchLimitOrder(order);
    }
}

void MatchingEngine::matchMarketOrder(const std::shared_ptr<Order>& order) {
    auto book = books_[order->symbol];
    double remainingQty = order->orderQty - order->cumQty;
    
    while (remainingQty > 0) {
        auto counterOrder = (order->side == Side::BUY) 
            ? book->getBestAskOrder() 
            : book->getBestBidOrder();
        
        if (!counterOrder) {
            order->state = OrderState::REJECTED;
            break;
        }
        
        double fillQty = std::min(remainingQty, 
                                  counterOrder->orderQty - counterOrder->cumQty);
        double fillPrice = counterOrder->price;
        
        executeFill(order, counterOrder, fillPrice, fillQty);
        
        remainingQty -= fillQty;
        
        if (counterOrder->cumQty >= counterOrder->orderQty) {
            book->removeOrder(counterOrder->orderId, counterOrder->side);
        }
    }
    
    if (order->cumQty >= order->orderQty) {
        order->state = OrderState::FILLED;
    } else if (order->cumQty > 0) {
        order->state = OrderState::PARTIALLY_FILLED;
    }
}

void MatchingEngine::matchLimitOrder(const std::shared_ptr<Order>& order) {
    auto book = books_[order->symbol];
    double remainingQty = order->orderQty - order->cumQty;
    
    while (remainingQty > 0) {
        auto counterOrder = (order->side == Side::BUY) 
            ? book->getBestAskOrder() 
            : book->getBestBidOrder();
        
        if (!counterOrder) break;
        
        bool canMatch = (order->side == Side::BUY) 
            ? (order->price >= counterOrder->price)
            : (order->price <= counterOrder->price);
        
        if (!canMatch) break;
        
        double fillQty = std::min(remainingQty, 
                                  counterOrder->orderQty - counterOrder->cumQty);
        double fillPrice = counterOrder->price;
        
        executeFill(order, counterOrder, fillPrice, fillQty);
        
        remainingQty -= fillQty;
        
        if (counterOrder->cumQty >= counterOrder->orderQty) {
            book->removeOrder(counterOrder->orderId, counterOrder->side);
        }
    }
    
    if (order->cumQty >= order->orderQty) {
        order->state = OrderState::FILLED;
    } else if (order->cumQty > 0) {
        order->state = OrderState::PARTIALLY_FILLED;
        addToBook(order);
    } else {
        order->state = OrderState::NEW;
        addToBook(order);
    }
}

void MatchingEngine::executeFill(const std::shared_ptr<Order>& aggressorOrder,
                                 const std::shared_ptr<Order>& restingOrder,
                                 double fillPrice, double fillQty) {
    aggressorOrder->cumQty += fillQty;
    aggressorOrder->avgPx = ((aggressorOrder->avgPx * (aggressorOrder->cumQty - fillQty)) 
                             + (fillPrice * fillQty)) / aggressorOrder->cumQty;
    
    restingOrder->cumQty += fillQty;
    restingOrder->avgPx = ((restingOrder->avgPx * (restingOrder->cumQty - fillQty)) 
                           + (fillPrice * fillQty)) / restingOrder->cumQty;
    
    std::string buyOrderId = (aggressorOrder->side == Side::BUY) 
        ? aggressorOrder->orderId : restingOrder->orderId;
    std::string sellOrderId = (aggressorOrder->side == Side::SELL) 
        ? aggressorOrder->orderId : restingOrder->orderId;
    
    Fill fill(buyOrderId, sellOrderId, aggressorOrder->symbol, fillPrice, fillQty,
              aggressorOrder->orderId);
    
    recentFills_[aggressorOrder->symbol].push_back(fill);
    if (recentFills_[aggressorOrder->symbol].size() > 1000) {
        recentFills_[aggressorOrder->symbol].pop_front();
    }
    
    if (fillCallback_) {
        fillCallback_(fill);
    }
    
    Trade trade(aggressorOrder->symbol, fillPrice, fillQty, generateTradeId());
    if (tradeCallback_) {
        tradeCallback_(trade);
    }
    
    priceGen_.setLastTradePrice(aggressorOrder->symbol, fillPrice);
    checkStopOrders(aggressorOrder->symbol, fillPrice);
}

void MatchingEngine::addToBook(const std::shared_ptr<Order>& order) {
    auto book = books_[order->symbol];
    book->addOrder(order);
}

void MatchingEngine::cancelOrder(const std::string& orderId, const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = books_.find(symbol);
    if (it != books_.end()) {
        it->second->removeOrder(orderId, Side::BUY);
        it->second->removeOrder(orderId, Side::SELL);
    }
    
    auto stopIt = stopOrders_.find(symbol);
    if (stopIt != stopOrders_.end()) {
        auto& orders = stopIt->second;
        orders.erase(
            std::remove_if(orders.begin(), orders.end(),
                [&orderId](const auto& order) { return order->orderId == orderId; }),
            orders.end());
    }
}

void MatchingEngine::checkStopOrders(const std::string& symbol, double lastPrice) {
    auto it = stopOrders_.find(symbol);
    if (it == stopOrders_.end()) return;
    
    auto& orders = it->second;
    std::vector<std::shared_ptr<Order>> triggered;
    
    for (auto& order : orders) {
        bool shouldTrigger = false;
        
        if (order->side == Side::BUY && lastPrice >= order->stopPx) {
            shouldTrigger = true;
        } else if (order->side == Side::SELL && lastPrice <= order->stopPx) {
            shouldTrigger = true;
        }
        
        if (shouldTrigger) {
            triggered.push_back(order);
            
            if (order->orderType == OrderType::STOP) {
                order->orderType = OrderType::MARKET;
            } else {
                order->orderType = OrderType::LIMIT;
            }
        }
    }
    
    for (const auto& order : triggered) {
        orders.erase(
            std::remove_if(orders.begin(), orders.end(),
                [&order](const auto& o) { return o->orderId == order->orderId; }),
            orders.end());
        matchOrder(order);
    }
}

void MatchingEngine::setFillCallback(FillCallback callback) {
    fillCallback_ = std::move(callback);
}

void MatchingEngine::setTradeCallback(TradeCallback callback) {
    tradeCallback_ = std::move(callback);
}

std::shared_ptr<OrderBookLevel> MatchingEngine::getOrderBook(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = books_.find(symbol);
    return (it != books_.end()) ? it->second : nullptr;
}

std::vector<Fill> MatchingEngine::getRecentFills(const std::string& symbol, size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = recentFills_.find(symbol);
    if (it == recentFills_.end()) return {};
    
    const auto& fills = it->second;
    size_t count = std::min(limit, fills.size());
    
    return std::vector<Fill>(fills.end() - count, fills.end());
}

std::string MatchingEngine::generateTradeId() {
    std::ostringstream oss;
    oss << "T" << std::setw(10) << std::setfill('0') << ++tradeCounter_;
    return oss.str();
}

}
