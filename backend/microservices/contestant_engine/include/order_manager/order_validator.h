#pragma once

#include "order_types.h"
#include "utils/config_loader.h"
#include <string>
#include <memory>
#include <unordered_set>

namespace fix_gateway {
namespace order_manager {

enum class ValidationResult {
    VALID,
    INVALID_SYMBOL,
    INVALID_PRICE,
    INVALID_QUANTITY,
    QUANTITY_TOO_SMALL,
    QUANTITY_TOO_LARGE,
    DUPLICATE_ORDER,
    MISSING_PRICE,
    INVALID_STOP_PRICE,
    OUTSIDE_TRADING_HOURS,
    INVALID_ACCOUNT
};

class OrderValidator {
public:
    explicit OrderValidator(const utils::TradingRules& rules);
    
    ValidationResult validate(const Order& order) const;
    ValidationResult validateCancel(const std::string& orderId, const Order* existingOrder) const;
    ValidationResult validateReplace(const Order& newOrder, const Order* existingOrder) const;
    
    bool isDuplicateOrder(const std::string& clOrdId) const;
    void markOrderProcessed(const std::string& clOrdId);
    void clearProcessedOrders();
    
    std::string getValidationMessage(ValidationResult result) const;

private:
    utils::TradingRules rules_;
    mutable std::unordered_set<std::string> processedClOrdIds_;
    mutable std::mutex mutex_;
    
    bool isSymbolTradable(const std::string& symbol) const;
    bool isPriceValid(double price) const;
    bool isQuantityValid(double qty) const;
    bool isWithinTradingHours() const;
};

}
}
