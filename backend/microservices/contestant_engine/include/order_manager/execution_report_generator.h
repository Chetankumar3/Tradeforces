#pragma once

#include "order_types.h"
#include "fix_engine/fix_message.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace fix_gateway {
namespace order_manager {

class ExecutionReportGenerator {
public:
    ExecutionReportGenerator() = default;
    
    fix_engine::FIXMessage generateNewOrderAck(const Order& order, const std::string& execId);
    fix_engine::FIXMessage generateRejection(const Order& order, const std::string& reason);
    fix_engine::FIXMessage generateFill(const Order& order, double fillQty, double fillPx,
                                       const std::string& execId, bool isFinal = false,
                                       bool isAggressor = false);
    fix_engine::FIXMessage generateCancelAck(const Order& order, const std::string& execId);
    fix_engine::FIXMessage generateCancelReject(const Order& order, const std::string& reason);
    fix_engine::FIXMessage generateReplaceAck(const Order& oldOrder, const Order& newOrder, 
                                             const std::string& execId);
    
    std::string generateExecId();
    
private:
    void setCommonFields(fix_engine::FIXMessage& msg, const Order& order);
    void setExecutionFields(fix_engine::FIXMessage& msg, ExecType execType, OrdStatus ordStatus);
    
    std::atomic<uint64_t> execIdCounter_{1};
};

}
}
