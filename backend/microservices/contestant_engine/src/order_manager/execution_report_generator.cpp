#include "order_manager/execution_report_generator.h"
#include <sstream>
#include <iomanip>

namespace fix_gateway {
namespace order_manager {

using namespace fix_engine;

fix_engine::FIXMessage ExecutionReportGenerator::generateNewOrderAck(
    const Order& order, const std::string& execId) {
    
    FIXMessage msg(MsgType::EXECUTION_REPORT);
    setCommonFields(msg, order);
    
    msg.setField(17, execId);
    msg.setField(150, std::string(1, static_cast<char>(ExecType::NEW)));
    msg.setField(39, std::string(1, static_cast<char>(OrdStatus::NEW)));
    msg.setField(151, std::to_string(order.leavesQty));
    
    return msg;
}

fix_engine::FIXMessage ExecutionReportGenerator::generateRejection(
    const Order& order, const std::string& reason) {
    
    FIXMessage msg(MsgType::EXECUTION_REPORT);
    setCommonFields(msg, order);
    
    msg.setField(17, generateExecId());
    msg.setField(150, std::string(1, static_cast<char>(ExecType::REJECTED)));
    msg.setField(39, std::string(1, static_cast<char>(OrdStatus::REJECTED)));
    msg.setField(151, std::to_string(order.orderQty));
    msg.setField(58, reason);
    
    return msg;
}

fix_engine::FIXMessage ExecutionReportGenerator::generateFill(
    const Order& order, double fillQty, double fillPx,
    const std::string& execId, bool isFinal, bool isAggressor) {
    
    FIXMessage msg(MsgType::EXECUTION_REPORT);
    setCommonFields(msg, order);
    
    msg.setField(17, execId);
    
    if (isFinal) {
        msg.setField(150, std::string(1, static_cast<char>(ExecType::FILL)));
        msg.setField(39, std::string(1, static_cast<char>(OrdStatus::FILLED)));
    } else {
        msg.setField(150, std::string(1, static_cast<char>(ExecType::PARTIAL_FILL)));
        msg.setField(39, std::string(1, static_cast<char>(OrdStatus::PARTIALLY_FILLED)));
    }
    
    msg.setField(31, std::to_string(fillPx));
    msg.setField(32, std::to_string(fillQty));
    msg.setField(151, std::to_string(order.leavesQty));
    msg.setField(1057, isAggressor ? "Y" : "N");
    
    return msg;
}

fix_engine::FIXMessage ExecutionReportGenerator::generateCancelAck(
    const Order& order, const std::string& execId) {
    
    FIXMessage msg(MsgType::EXECUTION_REPORT);
    setCommonFields(msg, order);
    
    msg.setField(17, execId);
    msg.setField(150, std::string(1, static_cast<char>(ExecType::CANCELED)));
    msg.setField(39, std::string(1, static_cast<char>(OrdStatus::CANCELED)));
    msg.setField(151, "0");
    
    return msg;
}

fix_engine::FIXMessage ExecutionReportGenerator::generateCancelReject(
    const Order& order, const std::string& reason) {
    
    FIXMessage msg(MsgType::EXECUTION_REPORT);
    setCommonFields(msg, order);
    
    msg.setField(17, generateExecId());
    msg.setField(150, std::string(1, static_cast<char>(ExecType::REJECTED)));
    msg.setField(39, std::string(1, static_cast<char>(OrdStatus::REJECTED)));
    msg.setField(58, reason);
    
    return msg;
}

fix_engine::FIXMessage ExecutionReportGenerator::generateReplaceAck(
    const Order& oldOrder, const Order& newOrder, const std::string& execId) {
    
    FIXMessage msg(MsgType::EXECUTION_REPORT);
    setCommonFields(msg, newOrder);
    
    msg.setField(17, execId);
    msg.setField(150, std::string(1, static_cast<char>(ExecType::REPLACED)));
    msg.setField(39, std::string(1, static_cast<char>(OrdStatus::REPLACED)));
    msg.setField(151, std::to_string(newOrder.leavesQty));
    msg.setField(41, oldOrder.orderId);
    
    return msg;
}

void ExecutionReportGenerator::setCommonFields(fix_engine::FIXMessage& msg, const Order& order) {
    msg.setField(37, order.orderId);
    msg.setField(11, order.clOrdId);
    msg.setField(55, order.symbol);
    msg.setField(54, std::to_string(static_cast<int>(order.side)));
    msg.setField(38, std::to_string(order.orderQty));
    msg.setField(40, std::to_string(static_cast<int>(order.orderType)));
    msg.setField(14, std::to_string(order.cumQty));
    msg.setField(6, std::to_string(order.avgPx));
    
    if (order.orderType == OrderType::LIMIT || order.orderType == OrderType::STOP_LIMIT) {
        msg.setField(44, std::to_string(order.price));
    }
    
    if (!order.account.empty()) {
        msg.setField(1, order.account);
    }
}

std::string ExecutionReportGenerator::generateExecId() {
    auto counter = execIdCounter_++;
    std::ostringstream oss;
    oss << "EXEC" << std::setfill('0') << std::setw(10) << counter;
    return oss.str();
}

}
}
