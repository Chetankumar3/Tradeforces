#pragma once

#include "order_types.h"
#include "order_book.h"
#include "position_tracker.h"
#include "order_validator.h"
#include "execution_report_generator.h"
#include "fix_engine/fix_message.h"
#include "utils/logger.h"
#include "risk/risk_manager.h"
#include "metrics/metrics_collector.h"
#include <memory>
#include <functional>
#include <mutex>

namespace fix_gateway {
namespace order_manager {

using ExecutionReportCallback = std::function<void(const fix_engine::FIXMessage&)>;
using OrderSubmitCallback = std::function<void(const std::shared_ptr<Order>&)>;

class OrderManager {
public:
    explicit OrderManager(const utils::TradingRules& rules);
    
    void setExecutionReportCallback(ExecutionReportCallback callback);
    void setOrderSubmitCallback(OrderSubmitCallback callback);
    
    void handleNewOrderSingle(const fix_engine::FIXMessage& msg);
    void handleOrderCancelRequest(const fix_engine::FIXMessage& msg);
    void handleOrderCancelReplaceRequest(const fix_engine::FIXMessage& msg);
    
    std::shared_ptr<Order> getOrder(const std::string& orderId) const;
    std::vector<std::shared_ptr<Order>> getActiveOrders() const;
    Position getPosition(const std::string& account, const std::string& symbol) const;
    
    void simulateFill(const std::string& orderId, double fillQty, double fillPx,
                      bool isAggressor = false);
    
    size_t getOrderCount() const;
    size_t getActiveOrderCount() const;

private:
    OrderBook orderBook_;
    PositionTracker positionTracker_;
    OrderValidator validator_;
    ExecutionReportGenerator execReportGen_;
    
    ExecutionReportCallback execReportCallback_;
    OrderSubmitCallback orderSubmitCallback_;
    std::shared_ptr<utils::SimpleLogger> logger_;
    
    mutable std::mutex mutex_;
    std::atomic<uint64_t> orderIdCounter_{1};
    
    std::shared_ptr<Order> parseNewOrderSingle(const fix_engine::FIXMessage& msg);
    void processNewOrder(std::shared_ptr<Order> order);
    void rejectOrder(std::shared_ptr<Order> order, const std::string& reason);
    void acceptOrder(std::shared_ptr<Order> order);
    
    void processCancelRequest(const std::string& orderId, const std::string& clOrdId,
                             const std::string& origClOrdId);
    void processReplaceRequest(const std::string& origClOrdId, double newQty, 
                              double newPrice, const std::string& clOrdId);
    
    void fillOrder(std::shared_ptr<Order> order, double fillQty, double fillPx,
                   bool isAggressor = false);
    void updateOrderState(std::shared_ptr<Order> order, OrderState newState);
    
    void sendExecutionReport(const fix_engine::FIXMessage& report);
    std::string generateOrderId();
};

}
}
