#pragma once

#include "risk_types.h"
#include "../order_manager/order_types.h"
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace risk {

class RiskManager {
public:
    static RiskManager& getInstance();
    
    void setAccountLimits(const std::string& account_id, const RiskLimits& limits);
    void setSymbolLimits(const std::string& symbol, const RiskLimits& limits);
    void setGlobalLimits(const RiskLimits& limits);
    
    RiskCheckResult checkOrder(const fix_gateway::order_manager::Order& order, double market_price);
    
    void updatePosition(const std::string& account_id, const std::string& symbol, 
                       int64_t quantity_change, double value_change);
    void updateDailyPnL(const std::string& account_id, double pnl_change);
    void updateCreditUsage(const std::string& account_id, double credit_change);
    
    RiskMetrics getAccountMetrics(const std::string& account_id) const;
    RiskMetrics getSymbolMetrics(const std::string& symbol) const;
    
    void resetDailyLimits();
    void enableSymbol(const std::string& symbol, bool enabled);
    
    bool loadConfiguration(const std::string& config_path);
    
private:
    RiskManager() = default;
    ~RiskManager() = default;
    RiskManager(const RiskManager&) = delete;
    RiskManager& operator=(const RiskManager&) = delete;
    
    RiskCheckResult checkOrderSize(const fix_gateway::order_manager::Order& order, const RiskLimits& limits) const;
    RiskCheckResult checkPositionLimits(const fix_gateway::order_manager::Order& order, const RiskLimits& limits) const;
    RiskCheckResult checkDailyLoss(const std::string& account_id, const RiskLimits& limits) const;
    RiskCheckResult checkCreditLimit(const std::string& account_id, double order_value, 
                                    const RiskLimits& limits) const;
    RiskCheckResult checkFatFinger(const fix_gateway::order_manager::Order& order, double market_price, 
                                  const RiskLimits& limits) const;
    RiskCheckResult checkConcentration(const fix_gateway::order_manager::Order& order, const RiskLimits& limits) const;
    
    RiskLimits getEffectiveLimits(const std::string& account_id, const std::string& symbol) const;
    
    mutable std::mutex mutex_;
    RiskLimits global_limits_;
    std::unordered_map<std::string, RiskLimits> account_limits_;
    std::unordered_map<std::string, RiskLimits> symbol_limits_;
    std::unordered_map<std::string, RiskMetrics> account_metrics_;
    std::unordered_map<std::string, RiskMetrics> symbol_metrics_;
    std::unordered_map<std::string, std::unordered_map<std::string, int64_t>> positions_;
    std::unordered_map<std::string, bool> symbol_enabled_;
};

}
