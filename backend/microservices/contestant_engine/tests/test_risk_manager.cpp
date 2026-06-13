#include <gtest/gtest.h>
#include "risk/risk_manager.h"
#include "order_manager/order_types.h"

using namespace risk;
using namespace fix_gateway::order_manager;

class RiskManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        RiskLimits limits;
        limits.max_order_quantity = 1000;
        limits.max_order_value = 100000.0;
        limits.max_position_quantity = 5000;
        limits.daily_loss_limit = 10000.0;
        limits.credit_limit = 500000.0;
        limits.fat_finger_threshold = 0.10;
        
        RiskManager::getInstance().setGlobalLimits(limits);
    }
};

TEST_F(RiskManagerTest, ApproveValidOrder) {
    Order order;
    order.symbol = "AAPL";
    order.side = Side::BUY;
    order.orderQty = 100;
    order.price = 150.0;
    order.account = "TEST001";
    
    auto result = RiskManager::getInstance().checkOrder(order, 150.0);
    EXPECT_EQ(result, RiskCheckResult::APPROVED);
}

TEST_F(RiskManagerTest, RejectExcessiveOrderSize) {
    Order order;
    order.symbol = "AAPL";
    order.side = Side::BUY;
    order.orderQty = 2000;
    order.price = 150.0;
    order.account = "TEST001";
    
    auto result = RiskManager::getInstance().checkOrder(order, 150.0);
    EXPECT_EQ(result, RiskCheckResult::REJECTED_MAX_ORDER_SIZE);
}

TEST_F(RiskManagerTest, RejectFatFingerOrder) {
    Order order;
    order.symbol = "AAPL";
    order.side = Side::BUY;
    order.orderQty = 100;
    order.price = 200.0;
    order.account = "TEST001";
    
    auto result = RiskManager::getInstance().checkOrder(order, 150.0);
    EXPECT_EQ(result, RiskCheckResult::REJECTED_FAT_FINGER);
}

TEST_F(RiskManagerTest, UpdatePosition) {
    auto& riskMgr = RiskManager::getInstance();
    riskMgr.updatePosition("TEST001", "AAPL", 100, 15000.0);
    
    auto metrics = riskMgr.getAccountMetrics("TEST001");
    EXPECT_EQ(metrics.current_position_value, 15000.0);
}

TEST_F(RiskManagerTest, TrackDailyPnL) {
    auto& riskMgr = RiskManager::getInstance();
    riskMgr.updateDailyPnL("TEST001", -5000.0);
    
    auto metrics = riskMgr.getAccountMetrics("TEST001");
    EXPECT_EQ(metrics.daily_pnl, -5000.0);
}

TEST_F(RiskManagerTest, RejectDailyLossLimitExceeded) {
    auto& riskMgr = RiskManager::getInstance();
    riskMgr.updateDailyPnL("TEST001", -15000.0);
    
    Order order;
    order.symbol = "AAPL";
    order.side = Side::BUY;
    order.orderQty = 100;
    order.price = 150.0;
    order.account = "TEST001";
    
    auto result = riskMgr.checkOrder(order, 150.0);
    EXPECT_EQ(result, RiskCheckResult::REJECTED_DAILY_LOSS_LIMIT);
}

TEST_F(RiskManagerTest, AccountSpecificLimits) {
    RiskLimits limits;
    limits.max_order_quantity = 500;
    RiskManager::getInstance().setAccountLimits("TEST002", limits);
    
    Order order;
    order.symbol = "AAPL";
    order.side = Side::BUY;
    order.orderQty = 600;
    order.price = 150.0;
    order.account = "TEST002";
    
    auto result = RiskManager::getInstance().checkOrder(order, 150.0);
    EXPECT_EQ(result, RiskCheckResult::REJECTED_MAX_ORDER_SIZE);
}

TEST_F(RiskManagerTest, SymbolDisabled) {
    RiskManager::getInstance().enableSymbol("AAPL", false);
    
    Order order;
    order.symbol = "AAPL";
    order.side = Side::BUY;
    order.orderQty = 100;
    order.price = 150.0;
    order.account = "TEST001";
    
    auto result = RiskManager::getInstance().checkOrder(order, 150.0);
    EXPECT_EQ(result, RiskCheckResult::REJECTED_SYMBOL_DISABLED);
}

TEST_F(RiskManagerTest, ResetDailyLimits) {
    auto& riskMgr = RiskManager::getInstance();
    riskMgr.updateDailyPnL("TEST001", -5000.0);
    riskMgr.resetDailyLimits();
    
    auto metrics = riskMgr.getAccountMetrics("TEST001");
    EXPECT_EQ(metrics.daily_pnl, 0.0);
}

TEST_F(RiskManagerTest, CreditLimitCheck) {
    auto& riskMgr = RiskManager::getInstance();
    riskMgr.updateCreditUsage("TEST001", 450000.0);
    
    Order order;
    order.symbol = "AAPL";
    order.side = Side::BUY;
    order.orderQty = 500;
    order.price = 150.0;
    order.account = "TEST001";
    
    auto result = riskMgr.checkOrder(order, 150.0);
    EXPECT_EQ(result, RiskCheckResult::REJECTED_CREDIT_LIMIT);
}
