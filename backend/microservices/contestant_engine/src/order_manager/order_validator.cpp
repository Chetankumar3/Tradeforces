#include "order_manager/order_validator.h"
#include <algorithm>
#include <cmath>

namespace fix_gateway {
namespace order_manager {

OrderValidator::OrderValidator(const utils::TradingRules& rules) 
    : rules_(rules) {
}

ValidationResult OrderValidator::validate(const Order& order) const {
    if (!isSymbolTradable(order.symbol)) {
        return ValidationResult::INVALID_SYMBOL;
    }
    
    if (!isQuantityValid(order.orderQty)) {
        return ValidationResult::INVALID_QUANTITY;
    }
    
    if (order.orderQty < rules_.minOrderSize) {
        return ValidationResult::QUANTITY_TOO_SMALL;
    }
    
    if (order.orderQty > rules_.maxOrderSize) {
        return ValidationResult::QUANTITY_TOO_LARGE;
    }
    
    if (isDuplicateOrder(order.clOrdId)) {
        return ValidationResult::DUPLICATE_ORDER;
    }
    
    if (order.orderType == OrderType::LIMIT || order.orderType == OrderType::STOP_LIMIT) {
        if (!isPriceValid(order.price) || order.price <= 0.0) {
            return ValidationResult::INVALID_PRICE;
        }
    }
    
    if (order.orderType == OrderType::STOP || order.orderType == OrderType::STOP_LIMIT) {
        if (!isPriceValid(order.stopPx) || order.stopPx <= 0.0) {
            return ValidationResult::INVALID_STOP_PRICE;
        }
    }
    
    if (order.orderType == OrderType::LIMIT && order.price <= 0.0) {
        return ValidationResult::MISSING_PRICE;
    }
    
    if (!isWithinTradingHours()) {
        return ValidationResult::OUTSIDE_TRADING_HOURS;
    }
    
    return ValidationResult::VALID;
}

ValidationResult OrderValidator::validateCancel(const std::string& orderId, 
                                                const Order* existingOrder) const {
    if (!existingOrder) {
        return ValidationResult::INVALID_SYMBOL;
    }
    
    if (!existingOrder->isActive()) {
        return ValidationResult::INVALID_SYMBOL;
    }
    
    return ValidationResult::VALID;
}

ValidationResult OrderValidator::validateReplace(const Order& newOrder, 
                                                 const Order* existingOrder) const {
    if (!existingOrder) {
        return ValidationResult::INVALID_SYMBOL;
    }
    
    if (!existingOrder->isActive()) {
        return ValidationResult::INVALID_SYMBOL;
    }
    
    if (!isQuantityValid(newOrder.orderQty)) {
        return ValidationResult::INVALID_QUANTITY;
    }
    
    if (newOrder.orderQty < rules_.minOrderSize) {
        return ValidationResult::QUANTITY_TOO_SMALL;
    }
    
    if (newOrder.orderQty > rules_.maxOrderSize) {
        return ValidationResult::QUANTITY_TOO_LARGE;
    }
    
    if (existingOrder->orderType == OrderType::LIMIT || 
        existingOrder->orderType == OrderType::STOP_LIMIT) {
        if (!isPriceValid(newOrder.price) || newOrder.price <= 0.0) {
            return ValidationResult::INVALID_PRICE;
        }
    }
    
    return ValidationResult::VALID;
}

bool OrderValidator::isDuplicateOrder(const std::string& clOrdId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return processedClOrdIds_.find(clOrdId) != processedClOrdIds_.end();
}

void OrderValidator::markOrderProcessed(const std::string& clOrdId) {
    std::lock_guard<std::mutex> lock(mutex_);
    processedClOrdIds_.insert(clOrdId);
}

void OrderValidator::clearProcessedOrders() {
    std::lock_guard<std::mutex> lock(mutex_);
    processedClOrdIds_.clear();
}

std::string OrderValidator::getValidationMessage(ValidationResult result) const {
    switch (result) {
        case ValidationResult::VALID:
            return "Order is valid";
        case ValidationResult::INVALID_SYMBOL:
            return "Invalid or non-tradable symbol";
        case ValidationResult::INVALID_PRICE:
            return "Invalid price";
        case ValidationResult::INVALID_QUANTITY:
            return "Invalid quantity";
        case ValidationResult::QUANTITY_TOO_SMALL:
            return "Quantity below minimum";
        case ValidationResult::QUANTITY_TOO_LARGE:
            return "Quantity exceeds maximum";
        case ValidationResult::DUPLICATE_ORDER:
            return "Duplicate ClOrdID";
        case ValidationResult::MISSING_PRICE:
            return "Price required for limit order";
        case ValidationResult::INVALID_STOP_PRICE:
            return "Invalid stop price";
        case ValidationResult::OUTSIDE_TRADING_HOURS:
            return "Outside trading hours";
        case ValidationResult::INVALID_ACCOUNT:
            return "Invalid account";
        default:
            return "Unknown validation error";
    }
}

bool OrderValidator::isSymbolTradable(const std::string& symbol) const {
    return std::find(rules_.tradableSymbols.begin(), 
                    rules_.tradableSymbols.end(), 
                    symbol) != rules_.tradableSymbols.end();
}

bool OrderValidator::isPriceValid(double price) const {
    return std::isfinite(price) && price >= 0.0;
}

bool OrderValidator::isQuantityValid(double qty) const {
    return std::isfinite(qty) && qty > 0.0;
}

bool OrderValidator::isWithinTradingHours() const {
    return true;
}

}
}
