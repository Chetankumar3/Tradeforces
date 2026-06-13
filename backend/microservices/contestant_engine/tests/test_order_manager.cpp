#include <gtest/gtest.h>
#include "order_manager/order_manager.h"
#include "utils/config_loader.h"
#include "utils/logger.h"

using namespace fix_gateway::order_manager;
using namespace fix_gateway::utils;
using namespace fix_gateway::fix_engine;

class OrderManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::getInstance().initialize("logs");
        
        TradingRules rules;
        rules.minOrderSize = 1.0;
        rules.maxOrderSize = 10000.0;
        rules.tradableSymbols = {"AAPL", "GOOGL", "MSFT"};
        
        orderManager = std::make_unique<OrderManager>(rules);
        
        orderManager->setExecutionReportCallback([this](const FIXMessage& report) {
            executionReports.push_back(report);
        });
    }
    
    std::unique_ptr<OrderManager> orderManager;
    std::vector<FIXMessage> executionReports;
    
    FIXMessage createNewOrderMessage(const std::string& clOrdId, const std::string& symbol,
                                     Side side, double qty, OrderType type, double price = 0.0) {
        FIXMessage msg(MsgType::NEW_ORDER_SINGLE);
        msg.setField(11, clOrdId);
        msg.setField(55, symbol);
        msg.setField(54, std::to_string(static_cast<int>(side)));
        msg.setField(38, std::to_string(qty));
        msg.setField(40, std::to_string(static_cast<int>(type)));
        
        if (type == OrderType::LIMIT) {
            msg.setField(44, std::to_string(price));
        }
        
        return msg;
    }
};

TEST_F(OrderManagerTest, ProcessValidOrder) {
    auto msg = createNewOrderMessage("CL001", "AAPL", Side::BUY, 100, OrderType::LIMIT, 150.0);
    
    orderManager->handleNewOrderSingle(msg);
    
    EXPECT_EQ(executionReports.size(), 1);
    EXPECT_EQ(orderManager->getOrderCount(), 1);
    
    auto execType = executionReports[0].getField(150);
    EXPECT_EQ(*execType, std::string(1, static_cast<char>(ExecType::NEW)));
}

TEST_F(OrderManagerTest, RejectInvalidSymbol) {
    auto msg = createNewOrderMessage("CL001", "INVALID", Side::BUY, 100, OrderType::LIMIT, 150.0);
    
    orderManager->handleNewOrderSingle(msg);
    
    EXPECT_EQ(executionReports.size(), 1);
    EXPECT_EQ(orderManager->getOrderCount(), 0);
    
    auto execType = executionReports[0].getField(150);
    EXPECT_EQ(*execType, std::string(1, static_cast<char>(ExecType::REJECTED)));
}

TEST_F(OrderManagerTest, CancelOrder) {
    auto newOrderMsg = createNewOrderMessage("CL001", "AAPL", Side::BUY, 100, OrderType::LIMIT, 150.0);
    orderManager->handleNewOrderSingle(newOrderMsg);
    
    executionReports.clear();
    
    FIXMessage cancelMsg(MsgType::ORDER_CANCEL_REQUEST);
    cancelMsg.setField(11, "CL002");
    cancelMsg.setField(41, "CL001");
    
    orderManager->handleOrderCancelRequest(cancelMsg);
    
    EXPECT_EQ(executionReports.size(), 1);
    
    auto execType = executionReports[0].getField(150);
    EXPECT_EQ(*execType, std::string(1, static_cast<char>(ExecType::CANCELED)));
    EXPECT_EQ(executionReports[0].getField(11), std::optional<std::string>("CL002"));
}

TEST_F(OrderManagerTest, ReplaceOrder) {
    auto newOrderMsg = createNewOrderMessage("CL001", "AAPL", Side::BUY, 100, OrderType::LIMIT, 150.0);
    orderManager->handleNewOrderSingle(newOrderMsg);
    
    executionReports.clear();
    
    FIXMessage replaceMsg(MsgType::ORDER_CANCEL_REPLACE_REQUEST);
    replaceMsg.setField(11, "CL002");
    replaceMsg.setField(41, "CL001");
    replaceMsg.setField(38, "200");
    replaceMsg.setField(44, "155.0");
    
    orderManager->handleOrderCancelReplaceRequest(replaceMsg);
    
    EXPECT_EQ(executionReports.size(), 1);
    
    auto execType = executionReports[0].getField(150);
    EXPECT_EQ(*execType, std::string(1, static_cast<char>(ExecType::REPLACED)));
}

TEST_F(OrderManagerTest, FillOrder) {
    auto newOrderMsg = createNewOrderMessage("CL001", "AAPL", Side::BUY, 100, OrderType::LIMIT, 150.0);
    orderManager->handleNewOrderSingle(newOrderMsg);
    
    auto orders = orderManager->getActiveOrders();
    ASSERT_EQ(orders.size(), 1);
    
    executionReports.clear();
    
    orderManager->simulateFill(orders[0]->orderId, 100, 150.0);
    
    EXPECT_EQ(executionReports.size(), 1);
    
    auto execType = executionReports[0].getField(150);
    EXPECT_EQ(*execType, std::string(1, static_cast<char>(ExecType::FILL)));
}

TEST_F(OrderManagerTest, FillReportIncludesAggressorIndicator) {
    auto newOrderMsg = createNewOrderMessage("CL001", "AAPL", Side::BUY, 100, OrderType::LIMIT, 150.0);
    orderManager->handleNewOrderSingle(newOrderMsg);

    auto orders = orderManager->getActiveOrders();
    ASSERT_EQ(orders.size(), 1);

    executionReports.clear();

    orderManager->simulateFill(orders[0]->orderId, 50, 150.0, true);

    ASSERT_FALSE(executionReports.empty());
    auto aggressorIndicator = executionReports[0].getField(1057);
    ASSERT_TRUE(aggressorIndicator.has_value());
    EXPECT_EQ(*aggressorIndicator, "Y");
}

TEST_F(OrderManagerTest, PartialFill) {
    auto newOrderMsg = createNewOrderMessage("CL001", "AAPL", Side::BUY, 100, OrderType::LIMIT, 150.0);
    orderManager->handleNewOrderSingle(newOrderMsg);
    
    auto orders = orderManager->getActiveOrders();
    ASSERT_EQ(orders.size(), 1);
    
    executionReports.clear();
    
    orderManager->simulateFill(orders[0]->orderId, 50, 150.0);
    
    EXPECT_EQ(executionReports.size(), 1);
    
    auto execType = executionReports[0].getField(150);
    EXPECT_EQ(*execType, std::string(1, static_cast<char>(ExecType::PARTIAL_FILL)));
    
    EXPECT_EQ(orderManager->getActiveOrderCount(), 1);
}

TEST_F(OrderManagerTest, PositionTracking) {
    auto newOrderMsg = createNewOrderMessage("CL001", "AAPL", Side::BUY, 100, OrderType::LIMIT, 150.0);
    orderManager->handleNewOrderSingle(newOrderMsg);
    
    auto orders = orderManager->getActiveOrders();
    orderManager->simulateFill(orders[0]->orderId, 100, 150.0);
    
    auto position = orderManager->getPosition("DEFAULT", "AAPL");
    EXPECT_EQ(position.quantity, 100);
    EXPECT_EQ(position.avgPrice, 150.0);
}

TEST_F(OrderManagerTest, MarketOrder) {
    auto msg = createNewOrderMessage("CL001", "AAPL", Side::BUY, 100, OrderType::MARKET);
    
    orderManager->handleNewOrderSingle(msg);
    
    EXPECT_EQ(executionReports.size(), 1);
    EXPECT_EQ(orderManager->getOrderCount(), 1);
}
