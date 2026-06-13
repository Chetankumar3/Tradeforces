#include "../../include/risk/risk_types.h"

namespace risk {

std::string riskCheckResultToString(RiskCheckResult result) {
    switch (result) {
        case RiskCheckResult::APPROVED:
            return "APPROVED";
        case RiskCheckResult::REJECTED_MAX_ORDER_SIZE:
            return "REJECTED_MAX_ORDER_SIZE";
        case RiskCheckResult::REJECTED_POSITION_LIMIT:
            return "REJECTED_POSITION_LIMIT";
        case RiskCheckResult::REJECTED_DAILY_LOSS_LIMIT:
            return "REJECTED_DAILY_LOSS_LIMIT";
        case RiskCheckResult::REJECTED_CREDIT_LIMIT:
            return "REJECTED_CREDIT_LIMIT";
        case RiskCheckResult::REJECTED_FAT_FINGER:
            return "REJECTED_FAT_FINGER";
        case RiskCheckResult::REJECTED_CONCENTRATION:
            return "REJECTED_CONCENTRATION";
        case RiskCheckResult::REJECTED_SYMBOL_DISABLED:
            return "REJECTED_SYMBOL_DISABLED";
        default:
            return "UNKNOWN";
    }
}

}
