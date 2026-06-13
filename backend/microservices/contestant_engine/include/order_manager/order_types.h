#pragma once

#include <string>
#include <chrono>
#include <memory>

namespace fix_gateway {
namespace order_manager {

enum class OrderType {
    MARKET = 1,
    LIMIT = 2,
    STOP = 3,
    STOP_LIMIT = 4
};

enum class Side {
    BUY = 1,
    SELL = 2
};

enum class TimeInForce {
    DAY = 0,
    GTC = 1,
    IOC = 3,
    FOK = 4
};

enum class OrderState {
    PENDING_NEW,
    NEW,
    PARTIALLY_FILLED,
    FILLED,
    PENDING_CANCEL,
    CANCELED,
    PENDING_REPLACE,
    REPLACED,
    REJECTED,
    EXPIRED
};

enum class ExecType {
    NEW = '0',
    PARTIAL_FILL = '1',
    FILL = '2',
    DONE_FOR_DAY = '3',
    CANCELED = '4',
    REPLACED = '5',
    PENDING_CANCEL = '6',
    STOPPED = '7',
    REJECTED = '8',
    SUSPENDED = '9',
    PENDING_NEW = 'A',
    CALCULATED = 'B',
    EXPIRED = 'C',
    RESTATED = 'D',
    PENDING_REPLACE = 'E',
    TRADE = 'F',
    TRADE_CORRECT = 'G',
    TRADE_CANCEL = 'H',
    ORDER_STATUS = 'I'
};

enum class OrdStatus {
    NEW = '0',
    PARTIALLY_FILLED = '1',
    FILLED = '2',
    DONE_FOR_DAY = '3',
    CANCELED = '4',
    REPLACED = '5',
    PENDING_CANCEL = '6',
    STOPPED = '7',
    REJECTED = '8',
    SUSPENDED = '9',
    PENDING_NEW = 'A',
    CALCULATED = 'B',
    EXPIRED = 'C',
    ACCEPTED_FOR_BIDDING = 'D',
    PENDING_REPLACE = 'E'
};

struct Order {
    std::string orderId;
    std::string clOrdId;
    std::string origClOrdId;
    std::string symbol;
    std::string account;
    
    Side side;
    OrderType orderType;
    TimeInForce timeInForce;
    OrderState state;
    
    double orderQty{0.0};
    double price{0.0};
    double stopPx{0.0};
    
    double cumQty{0.0};
    double leavesQty{0.0};
    double avgPx{0.0};
    
    std::chrono::system_clock::time_point transactTime;
    std::chrono::system_clock::time_point creationTime;
    std::chrono::system_clock::time_point lastUpdateTime;
    
    std::string text;
    
    Order() : transactTime(std::chrono::system_clock::now()),
              creationTime(std::chrono::system_clock::now()),
              lastUpdateTime(std::chrono::system_clock::now()) {}
    
    double getRemainingQty() const { return orderQty - cumQty; }
    bool isFilled() const { return cumQty >= orderQty; }
    bool isActive() const { 
        return state == OrderState::NEW || 
               state == OrderState::PARTIALLY_FILLED ||
               state == OrderState::PENDING_CANCEL ||
               state == OrderState::PENDING_REPLACE;
    }
};

struct Execution {
    std::string execId;
    std::string orderId;
    std::string clOrdId;
    std::string symbol;
    
    ExecType execType;
    OrdStatus ordStatus;
    Side side;
    
    double orderQty{0.0};
    double cumQty{0.0};
    double leavesQty{0.0};
    double lastQty{0.0};
    double lastPx{0.0};
    double avgPx{0.0};
    
    std::chrono::system_clock::time_point transactTime;
    std::string text;
    
    Execution() : transactTime(std::chrono::system_clock::now()) {}
};

inline std::string toString(OrderType type) {
    switch(type) {
        case OrderType::MARKET: return "MARKET";
        case OrderType::LIMIT: return "LIMIT";
        case OrderType::STOP: return "STOP";
        case OrderType::STOP_LIMIT: return "STOP_LIMIT";
        default: return "UNKNOWN";
    }
}

inline std::string toString(Side side) {
    return side == Side::BUY ? "BUY" : "SELL";
}

inline std::string toString(OrderState state) {
    switch(state) {
        case OrderState::PENDING_NEW: return "PENDING_NEW";
        case OrderState::NEW: return "NEW";
        case OrderState::PARTIALLY_FILLED: return "PARTIALLY_FILLED";
        case OrderState::FILLED: return "FILLED";
        case OrderState::PENDING_CANCEL: return "PENDING_CANCEL";
        case OrderState::CANCELED: return "CANCELED";
        case OrderState::PENDING_REPLACE: return "PENDING_REPLACE";
        case OrderState::REPLACED: return "REPLACED";
        case OrderState::REJECTED: return "REJECTED";
        case OrderState::EXPIRED: return "EXPIRED";
        default: return "UNKNOWN";
    }
}

}
}
