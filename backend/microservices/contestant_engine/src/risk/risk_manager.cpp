#include "../../include/risk/risk_manager.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <cmath>
#include <algorithm>

using json = nlohmann::json;

namespace risk {

RiskManager& RiskManager::getInstance() {
    static RiskManager instance;
    return instance;
}

void RiskManager::setAccountLimits(const std::string& account_id, const RiskLimits& limits) {
    std::lock_guard<std::mutex> lock(mutex_);
    account_limits_[account_id] = limits;
}

void RiskManager::setSymbolLimits(const std::string& symbol, const RiskLimits& limits) {
    std::lock_guard<std::mutex> lock(mutex_);
    symbol_limits_[symbol] = limits;
}

void RiskManager::setGlobalLimits(const RiskLimits& limits) {
    std::lock_guard<std::mutex> lock(mutex_);
    global_limits_ = limits;
}

RiskCheckResult RiskManager::checkOrder(const fix_gateway::order_manager::Order& order, double market_price) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto limits = getEffectiveLimits(order.account, order.symbol);
    
    if (symbol_enabled_.count(order.symbol) && !symbol_enabled_[order.symbol]) {
        return RiskCheckResult::REJECTED_SYMBOL_DISABLED;
    }
    
    auto result = checkOrderSize(order, limits);
    if (result != RiskCheckResult::APPROVED) return result;
    
    result = checkPositionLimits(order, limits);
    if (result != RiskCheckResult::APPROVED) return result;
    
    result = checkDailyLoss(order.account, limits);
    if (result != RiskCheckResult::APPROVED) return result;
    
    double order_value = order.orderQty * (order.price > 0 ? order.price : market_price);
    result = checkCreditLimit(order.account, order_value, limits);
    if (result != RiskCheckResult::APPROVED) return result;
    
    if (market_price > 0 && order.price > 0) {
        result = checkFatFinger(order, market_price, limits);
        if (result != RiskCheckResult::APPROVED) return result;
    }
    
    result = checkConcentration(order, limits);
    if (result != RiskCheckResult::APPROVED) return result;
    
    account_metrics_[order.account].order_count++;
    symbol_metrics_[order.symbol].order_count++;
    
    return RiskCheckResult::APPROVED;
}

RiskCheckResult RiskManager::checkOrderSize(const fix_gateway::order_manager::Order& order, const RiskLimits& limits) const {
    if (order.orderQty > limits.max_order_quantity) {
        return RiskCheckResult::REJECTED_MAX_ORDER_SIZE;
    }
    
    double order_value = order.orderQty * order.price;
    if (order_value > limits.max_order_value) {
        return RiskCheckResult::REJECTED_MAX_ORDER_SIZE;
    }
    
    return RiskCheckResult::APPROVED;
}

RiskCheckResult RiskManager::checkPositionLimits(const fix_gateway::order_manager::Order& order, const RiskLimits& limits) const {
    int64_t current_position = 0;
    auto acc_it = positions_.find(order.account);
    if (acc_it != positions_.end()) {
        auto sym_it = acc_it->second.find(order.symbol);
        if (sym_it != acc_it->second.end()) {
            current_position = sym_it->second;
        }
    }
    
    int64_t new_position = current_position;
    if (order.side == fix_gateway::order_manager::Side::BUY) {
        new_position += order.orderQty;
    } else {
        new_position -= order.orderQty;
    }
    
    if (std::abs(new_position) > static_cast<int64_t>(limits.max_position_quantity)) {
        return RiskCheckResult::REJECTED_POSITION_LIMIT;
    }
    
    return RiskCheckResult::APPROVED;
}

RiskCheckResult RiskManager::checkDailyLoss(const std::string& account_id, const RiskLimits& limits) const {
    auto it = account_metrics_.find(account_id);
    if (it != account_metrics_.end()) {
        if (it->second.daily_pnl < -limits.daily_loss_limit) {
            return RiskCheckResult::REJECTED_DAILY_LOSS_LIMIT;
        }
    }
    return RiskCheckResult::APPROVED;
}

RiskCheckResult RiskManager::checkCreditLimit(const std::string& account_id, double order_value,
                                              const RiskLimits& limits) const {
    auto it = account_metrics_.find(account_id);
    if (it != account_metrics_.end()) {
        if (it->second.used_credit + order_value > limits.credit_limit) {
            return RiskCheckResult::REJECTED_CREDIT_LIMIT;
        }
    }
    return RiskCheckResult::APPROVED;
}

RiskCheckResult RiskManager::checkFatFinger(const fix_gateway::order_manager::Order& order, double market_price,
                                            const RiskLimits& limits) const {
    double deviation = std::abs(order.price - market_price) / market_price;
    if (deviation > limits.fat_finger_threshold) {
        return RiskCheckResult::REJECTED_FAT_FINGER;
    }
    return RiskCheckResult::APPROVED;
}

RiskCheckResult RiskManager::checkConcentration(const fix_gateway::order_manager::Order& order, const RiskLimits& limits) const {
    auto acc_it = account_metrics_.find(order.account);
    if (acc_it == account_metrics_.end()) return RiskCheckResult::APPROVED;
    
    double total_value = acc_it->second.current_position_value;
    if (total_value <= 0) return RiskCheckResult::APPROVED;
    
    double symbol_value = 0.0;
    auto pos_it = positions_.find(order.account);
    if (pos_it != positions_.end()) {
        auto sym_it = pos_it->second.find(order.symbol);
        if (sym_it != pos_it->second.end()) {
            symbol_value = std::abs(sym_it->second) * order.price;
        }
    }
    
    double concentration = symbol_value / total_value;
    if (concentration > limits.concentration_limit) {
        return RiskCheckResult::REJECTED_CONCENTRATION;
    }
    
    return RiskCheckResult::APPROVED;
}

void RiskManager::updatePosition(const std::string& account_id, const std::string& symbol,
                                int64_t quantity_change, double value_change) {
    std::lock_guard<std::mutex> lock(mutex_);
    positions_[account_id][symbol] += quantity_change;
    account_metrics_[account_id].current_position_value += value_change;
    symbol_metrics_[symbol].current_position_value += value_change;
}

void RiskManager::updateDailyPnL(const std::string& account_id, double pnl_change) {
    std::lock_guard<std::mutex> lock(mutex_);
    account_metrics_[account_id].daily_pnl += pnl_change;
}

void RiskManager::updateCreditUsage(const std::string& account_id, double credit_change) {
    std::lock_guard<std::mutex> lock(mutex_);
    account_metrics_[account_id].used_credit += credit_change;
}

RiskMetrics RiskManager::getAccountMetrics(const std::string& account_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = account_metrics_.find(account_id);
    return it != account_metrics_.end() ? it->second : RiskMetrics{};
}

RiskMetrics RiskManager::getSymbolMetrics(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = symbol_metrics_.find(symbol);
    return it != symbol_metrics_.end() ? it->second : RiskMetrics{};
}

void RiskManager::resetDailyLimits() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [account_id, metrics] : account_metrics_) {
        metrics.daily_pnl = 0.0;
        metrics.order_count = 0;
        metrics.rejected_count = 0;
    }
    for (auto& [symbol, metrics] : symbol_metrics_) {
        metrics.order_count = 0;
        metrics.rejected_count = 0;
    }
}

void RiskManager::enableSymbol(const std::string& symbol, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    symbol_enabled_[symbol] = enabled;
}

bool RiskManager::loadConfiguration(const std::string& config_path) {
    try {
        std::ifstream file(config_path);
        if (!file.is_open()) return false;
        
        json config = json::parse(file);
        
        if (config.contains("global_limits")) {
            auto& gl = config["global_limits"];
            RiskLimits limits;
            if (gl.contains("max_order_quantity")) limits.max_order_quantity = gl["max_order_quantity"];
            if (gl.contains("max_order_value")) limits.max_order_value = gl["max_order_value"];
            if (gl.contains("max_position_quantity")) limits.max_position_quantity = gl["max_position_quantity"];
            if (gl.contains("max_position_value")) limits.max_position_value = gl["max_position_value"];
            if (gl.contains("daily_loss_limit")) limits.daily_loss_limit = gl["daily_loss_limit"];
            if (gl.contains("credit_limit")) limits.credit_limit = gl["credit_limit"];
            if (gl.contains("fat_finger_threshold")) limits.fat_finger_threshold = gl["fat_finger_threshold"];
            if (gl.contains("concentration_limit")) limits.concentration_limit = gl["concentration_limit"];
            setGlobalLimits(limits);
        }
        
        if (config.contains("account_limits")) {
            for (auto& [account_id, al] : config["account_limits"].items()) {
                RiskLimits limits;
                if (al.contains("max_order_quantity")) limits.max_order_quantity = al["max_order_quantity"];
                if (al.contains("max_order_value")) limits.max_order_value = al["max_order_value"];
                if (al.contains("max_position_quantity")) limits.max_position_quantity = al["max_position_quantity"];
                if (al.contains("max_position_value")) limits.max_position_value = al["max_position_value"];
                if (al.contains("daily_loss_limit")) limits.daily_loss_limit = al["daily_loss_limit"];
                if (al.contains("credit_limit")) limits.credit_limit = al["credit_limit"];
                if (al.contains("fat_finger_threshold")) limits.fat_finger_threshold = al["fat_finger_threshold"];
                if (al.contains("concentration_limit")) limits.concentration_limit = al["concentration_limit"];
                setAccountLimits(account_id, limits);
            }
        }
        
        if (config.contains("symbol_limits")) {
            for (auto& [symbol, sl] : config["symbol_limits"].items()) {
                RiskLimits limits;
                if (sl.contains("max_order_quantity")) limits.max_order_quantity = sl["max_order_quantity"];
                if (sl.contains("max_order_value")) limits.max_order_value = sl["max_order_value"];
                if (sl.contains("max_position_quantity")) limits.max_position_quantity = sl["max_position_quantity"];
                if (sl.contains("max_position_value")) limits.max_position_value = sl["max_position_value"];
                setSymbolLimits(symbol, limits);
            }
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

RiskLimits RiskManager::getEffectiveLimits(const std::string& account_id, const std::string& symbol) const {
    RiskLimits limits = global_limits_;
    
    auto acc_it = account_limits_.find(account_id);
    if (acc_it != account_limits_.end()) {
        limits.max_order_quantity = std::min(limits.max_order_quantity, acc_it->second.max_order_quantity);
        limits.max_order_value = std::min(limits.max_order_value, acc_it->second.max_order_value);
        limits.max_position_quantity = std::min(limits.max_position_quantity, acc_it->second.max_position_quantity);
        limits.max_position_value = std::min(limits.max_position_value, acc_it->second.max_position_value);
        limits.daily_loss_limit = std::min(limits.daily_loss_limit, acc_it->second.daily_loss_limit);
        limits.credit_limit = std::min(limits.credit_limit, acc_it->second.credit_limit);
        limits.fat_finger_threshold = std::min(limits.fat_finger_threshold, acc_it->second.fat_finger_threshold);
        limits.concentration_limit = std::min(limits.concentration_limit, acc_it->second.concentration_limit);
    }
    
    auto sym_it = symbol_limits_.find(symbol);
    if (sym_it != symbol_limits_.end()) {
        limits.max_order_quantity = std::min(limits.max_order_quantity, sym_it->second.max_order_quantity);
        limits.max_order_value = std::min(limits.max_order_value, sym_it->second.max_order_value);
        limits.max_position_quantity = std::min(limits.max_position_quantity, sym_it->second.max_position_quantity);
        limits.max_position_value = std::min(limits.max_position_value, sym_it->second.max_position_value);
    }
    
    return limits;
}

}
