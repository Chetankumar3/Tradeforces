#include "order_manager/order_manager.h"
#include <sstream>
#include <iomanip>

namespace fix_gateway {
namespace order_manager {

OrderManager::OrderManager(const utils::TradingRules& rules)
    : validator_(rules)
    , logger_(utils::Logger::getInstance().getLogger("order_manager")) {
}

void OrderManager::setExecutionReportCallback(ExecutionReportCallback callback) {
    execReportCallback_ = callback;
}

void OrderManager::setOrderSubmitCallback(OrderSubmitCallback callback) {
    orderSubmitCallback_ = callback;
}

void OrderManager::handleNewOrderSingle(const fix_engine::FIXMessage& msg) {
    auto order = parseNewOrderSingle(msg);
    if (!order) {
        logger_->error("Failed to parse NewOrderSingle message");
        return;
    }
    
    std::ostringstream oss;
    oss << "Processing NewOrderSingle: ClOrdID=" << order->clOrdId 
        << ", Symbol=" << order->symbol << ", Side=" << toString(order->side)
        << ", Qty=" << order->orderQty << ", Price=" << order->price;
    logger_->info(oss.str());
    
    processNewOrder(order);
}

void OrderManager::handleOrderCancelRequest(const fix_engine::FIXMessage& msg) {
    auto clOrdId = msg.getField(11);
    auto origClOrdId = msg.getField(41);
    auto orderId = msg.getField(37);
    
    if (!origClOrdId && !orderId) {
        logger_->warn("CancelRequest missing OrigClOrdID and OrderID");
        return;
    }
    
    std::ostringstream oss;
    oss << "Processing OrderCancelRequest: ClOrdID=" << clOrdId.value_or("")
        << ", OrigClOrdID=" << origClOrdId.value_or("");
    logger_->info(oss.str());
    
    processCancelRequest(orderId.value_or(""), clOrdId.value_or(""), origClOrdId.value_or(""));
}

void OrderManager::handleOrderCancelReplaceRequest(const fix_engine::FIXMessage& msg) {
    auto clOrdId = msg.getField(11);
    auto origClOrdId = msg.getField(41);
    auto orderQty = msg.getField(38);
    auto price = msg.getField(44);
    
    if (!origClOrdId || !orderQty) {
        logger_->warn("CancelReplaceRequest missing required fields");
        return;
    }
    
    double newQty = std::stod(*orderQty);
    double newPrice = price ? std::stod(*price) : 0.0;
    
    std::ostringstream oss;
    oss << "Processing OrderCancelReplaceRequest: ClOrdID=" << clOrdId.value_or("")
        << ", OrigClOrdID=" << *origClOrdId << ", NewQty=" << newQty << ", NewPrice=" << newPrice;
    logger_->info(oss.str());
    
    processReplaceRequest(*origClOrdId, newQty, newPrice, clOrdId.value_or(""));
}

std::shared_ptr<Order> OrderManager::getOrder(const std::string& orderId) const {
    return orderBook_.getOrder(orderId);
}

std::vector<std::shared_ptr<Order>> OrderManager::getActiveOrders() const {
    return orderBook_.getActiveOrders();
}

Position OrderManager::getPosition(const std::string& account, const std::string& symbol) const {
    return positionTracker_.getPosition(account, symbol);
}

void OrderManager::simulateFill(const std::string& orderId, double fillQty, double fillPx,
                                bool isAggressor) {
    auto order = orderBook_.getOrder(orderId);
    if (!order) {
        logger_->warn("Order not found for fill: " + orderId);
        return;
    }
    
    if (!order->isActive()) {
        logger_->warn("Cannot fill inactive order: " + orderId);
        return;
    }
    
    fillOrder(order, fillQty, fillPx, isAggressor);
}

size_t OrderManager::getOrderCount() const {
    return orderBook_.getOrderCount();
}

size_t OrderManager::getActiveOrderCount() const {
    return orderBook_.getActiveOrderCount();
}

std::shared_ptr<Order> OrderManager::parseNewOrderSingle(const fix_engine::FIXMessage& msg) {
    auto order = std::make_shared<Order>();
    
    auto clOrdId = msg.getField(11);
    auto symbol = msg.getField(55);
    auto side = msg.getField(54);
    auto orderQty = msg.getField(38);
    auto ordType = msg.getField(40);
    
    if (!clOrdId || !symbol || !side || !orderQty || !ordType) {
        return nullptr;
    }
    
    order->clOrdId = *clOrdId;
    order->symbol = *symbol;
    order->side = static_cast<Side>(std::stoi(*side));
    order->orderQty = std::stod(*orderQty);
    order->leavesQty = order->orderQty;
    order->orderType = static_cast<OrderType>(std::stoi(*ordType));
    
    auto price = msg.getField(44);
    if (price) {
        order->price = std::stod(*price);
    }
    
    auto stopPx = msg.getField(99);
    if (stopPx) {
        order->stopPx = std::stod(*stopPx);
    }
    
    auto timeInForce = msg.getField(59);
    if (timeInForce) {
        order->timeInForce = static_cast<TimeInForce>(std::stoi(*timeInForce));
    } else {
        order->timeInForce = TimeInForce::DAY;
    }
    
    auto account = msg.getField(1);
    order->account = account.value_or("DEFAULT");
    
    order->state = OrderState::PENDING_NEW;
    order->transactTime = std::chrono::system_clock::now();
    
    return order;
}

void OrderManager::processNewOrder(std::shared_ptr<Order> order) {
    metrics::LatencyTimer timer;
    
    auto validationResult = validator_.validate(*order);
    
    if (validationResult != ValidationResult::VALID) {
        std::string reason = validator_.getValidationMessage(validationResult);
        logger_->warn("Order validation failed: " + reason);
        rejectOrder(order, reason);
        metrics::MetricsCollector::getInstance().recordOrderRejected();
        metrics::MetricsCollector::getInstance().recordOrderLatency(timer.elapsed());
        return;
    }
    
    auto& riskMgr = risk::RiskManager::getInstance();
    double marketPrice = order->price > 0 ? order->price : 100.0;
    auto riskResult = riskMgr.checkOrder(*order, marketPrice);
    
    if (riskResult != risk::RiskCheckResult::APPROVED) {
        std::string reason = "Risk check failed: " + risk::riskCheckResultToString(riskResult);
        logger_->warn("Order risk check failed: " + reason);
        rejectOrder(order, reason);
        metrics::MetricsCollector::getInstance().recordOrderRejected();
        metrics::MetricsCollector::getInstance().recordOrderLatency(timer.elapsed());
        return;
    }
    
    order->orderId = generateOrderId();
    validator_.markOrderProcessed(order->clOrdId);
    
    acceptOrder(order);
    metrics::MetricsCollector::getInstance().recordOrderSubmitted();
    metrics::MetricsCollector::getInstance().recordOrderLatency(timer.elapsed());
}

void OrderManager::rejectOrder(std::shared_ptr<Order> order, const std::string& reason) {
    order->state = OrderState::REJECTED;
    order->text = reason;
    
    auto report = execReportGen_.generateRejection(*order, reason);
    sendExecutionReport(report);
    
    std::ostringstream oss;
    oss << "Order rejected: ClOrdID=" << order->clOrdId << ", Reason=" << reason;
    logger_->info(oss.str());
}

void OrderManager::acceptOrder(std::shared_ptr<Order> order) {
    order->state = OrderState::NEW;
    orderBook_.addOrder(order);
    
    if (orderSubmitCallback_) {
        orderSubmitCallback_(order);
    }
    
    auto execId = execReportGen_.generateExecId();
    auto report = execReportGen_.generateNewOrderAck(*order, execId);
    sendExecutionReport(report);
    
    std::ostringstream oss;
    oss << "Order accepted: OrderID=" << order->orderId << ", ClOrdID=" << order->clOrdId;
    logger_->info(oss.str());
}

void OrderManager::processCancelRequest(const std::string& orderId, 
                                       const std::string& clOrdId,
                                       const std::string& origClOrdId) {
    std::shared_ptr<Order> order;
    
    if (!origClOrdId.empty()) {
        order = orderBook_.getOrderByClOrdId(origClOrdId);
    } else if (!orderId.empty()) {
        order = orderBook_.getOrder(orderId);
    }
    
    if (!order) {
        logger_->warn("Order not found for cancel: OrigClOrdID=" + origClOrdId);
        
        Order dummyOrder;
        dummyOrder.clOrdId = clOrdId;
        dummyOrder.origClOrdId = origClOrdId;
        auto report = execReportGen_.generateCancelReject(dummyOrder, "Order not found");
        sendExecutionReport(report);
        return;
    }
    
    auto validationResult = validator_.validateCancel(order->orderId, order.get());
    if (validationResult != ValidationResult::VALID) {
        std::string reason = validator_.getValidationMessage(validationResult);
        auto report = execReportGen_.generateCancelReject(*order, reason);
        if (!clOrdId.empty()) {
            report.setField(11, clOrdId);
        }
        sendExecutionReport(report);
        return;
    }
    
    updateOrderState(order, OrderState::CANCELED);
    
    auto execId = execReportGen_.generateExecId();
    auto report = execReportGen_.generateCancelAck(*order, execId);
    if (!clOrdId.empty()) {
        report.setField(11, clOrdId);
    }
    sendExecutionReport(report);
    
    metrics::MetricsCollector::getInstance().recordOrderCanceled();
    
    logger_->info("Order canceled: OrderID=" + order->orderId);
}

void OrderManager::processReplaceRequest(const std::string& origClOrdId, 
                                        double newQty, double newPrice,
                                        const std::string& clOrdId) {
    auto order = orderBook_.getOrderByClOrdId(origClOrdId);
    
    if (!order) {
        logger_->warn("Order not found for replace: OrigClOrdID=" + origClOrdId);
        
        Order dummyOrder;
        dummyOrder.clOrdId = clOrdId;
        dummyOrder.origClOrdId = origClOrdId;
        auto report = execReportGen_.generateCancelReject(dummyOrder, "Order not found");
        sendExecutionReport(report);
        return;
    }
    
    Order newOrder = *order;
    newOrder.orderQty = newQty;
    newOrder.price = newPrice;
    newOrder.leavesQty = newQty - order->cumQty;
    
    auto validationResult = validator_.validateReplace(newOrder, order.get());
    if (validationResult != ValidationResult::VALID) {
        std::string reason = validator_.getValidationMessage(validationResult);
        auto report = execReportGen_.generateCancelReject(*order, reason);
        sendExecutionReport(report);
        return;
    }
    
    auto oldOrder = *order;
    order->orderQty = newQty;
    order->price = newPrice;
    order->leavesQty = newQty - order->cumQty;
    order->clOrdId = clOrdId;
    order->origClOrdId = origClOrdId;
    order->lastUpdateTime = std::chrono::system_clock::now();
    
    orderBook_.updateOrder(order);
    
    auto execId = execReportGen_.generateExecId();
    auto report = execReportGen_.generateReplaceAck(oldOrder, *order, execId);
    sendExecutionReport(report);
    
    std::ostringstream oss;
    oss << "Order replaced: OrderID=" << order->orderId << ", NewQty=" << newQty << ", NewPrice=" << newPrice;
    logger_->info(oss.str());
}

void OrderManager::fillOrder(std::shared_ptr<Order> order, double fillQty, double fillPx,
                              bool isAggressor) {
    metrics::LatencyTimer execTimer;
    
    double actualFillQty = std::min(fillQty, order->leavesQty);
    
    double totalValue = order->cumQty * order->avgPx + actualFillQty * fillPx;
    order->cumQty += actualFillQty;
    order->avgPx = order->cumQty > 0 ? totalValue / order->cumQty : 0.0;
    order->leavesQty = order->orderQty - order->cumQty;
    order->lastUpdateTime = std::chrono::system_clock::now();
    
    bool isFilled = order->isFilled();
    
    if (isFilled) {
        updateOrderState(order, OrderState::FILLED);
        metrics::MetricsCollector::getInstance().recordOrderFilled();
    } else {
        updateOrderState(order, OrderState::PARTIALLY_FILLED);
    }
    
    positionTracker_.updatePosition(order->account, order->symbol, 
                                   order->side, actualFillQty, fillPx);
    
    auto& riskMgr = risk::RiskManager::getInstance();
    int64_t qty_change = (order->side == Side::BUY) ? actualFillQty : -actualFillQty;
    double value_change = actualFillQty * fillPx;
    riskMgr.updatePosition(order->account, order->symbol, qty_change, value_change);
    
    auto execId = execReportGen_.generateExecId();
    auto report = execReportGen_.generateFill(*order, actualFillQty, fillPx, execId,
                                              isFilled, isAggressor);
    sendExecutionReport(report);
    
    metrics::MetricsCollector::getInstance().recordExecutionLatency(execTimer.elapsed());
    
    std::ostringstream oss;
    oss << "Order filled: OrderID=" << order->orderId << ", FillQty=" << actualFillQty
        << ", FillPx=" << fillPx << ", CumQty=" << order->cumQty << ", LeavesQty=" << order->leavesQty;
    logger_->info(oss.str());
}

void OrderManager::updateOrderState(std::shared_ptr<Order> order, OrderState newState) {
    order->state = newState;
    order->lastUpdateTime = std::chrono::system_clock::now();
    orderBook_.updateOrder(order);
}

void OrderManager::sendExecutionReport(const fix_engine::FIXMessage& report) {
    if (execReportCallback_) {
        execReportCallback_(report);
    }
}

std::string OrderManager::generateOrderId() {
    auto counter = orderIdCounter_++;
    std::ostringstream oss;
    oss << "ORD" << std::setfill('0') << std::setw(10) << counter;
    return oss.str();
}

}
}
