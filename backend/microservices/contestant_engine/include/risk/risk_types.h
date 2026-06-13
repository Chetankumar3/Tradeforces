#pragma once

#include <string>
#include <cstdint>

namespace risk {

enum class RiskCheckResult {
    APPROVED,
    REJECTED_MAX_ORDER_SIZE,
    REJECTED_POSITION_LIMIT,
    REJECTED_DAILY_LOSS_LIMIT,
    REJECTED_CREDIT_LIMIT,
    REJECTED_FAT_FINGER,
    REJECTED_CONCENTRATION,
    REJECTED_SYMBOL_DISABLED
};

struct RiskLimits {
    uint64_t max_order_quantity = 10000;
    double max_order_value = 1000000.0;
    uint64_t max_position_quantity = 50000;
    double max_position_value = 5000000.0;
    double daily_loss_limit = 100000.0;
    double credit_limit = 10000000.0;
    double fat_finger_threshold = 0.10;
    double concentration_limit = 0.25;
    bool symbol_trading_enabled = true;
};

struct RiskMetrics {
    double current_position_value = 0.0;
    double daily_pnl = 0.0;
    double used_credit = 0.0;
    uint64_t order_count = 0;
    uint64_t rejected_count = 0;
};

std::string riskCheckResultToString(RiskCheckResult result);

}
