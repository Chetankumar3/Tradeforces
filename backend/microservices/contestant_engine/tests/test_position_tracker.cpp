#include <gtest/gtest.h>
#include "order_manager/position_tracker.h"

using namespace fix_gateway::order_manager;

class PositionTrackerTest : public ::testing::Test {
protected:
    void SetUp() override {
        tracker = std::make_unique<PositionTracker>();
    }
    
    std::unique_ptr<PositionTracker> tracker;
};

TEST_F(PositionTrackerTest, InitialPosition) {
    auto position = tracker->getPosition("ACC001", "AAPL");
    
    EXPECT_EQ(position.quantity, 0.0);
    EXPECT_EQ(position.avgPrice, 0.0);
    EXPECT_EQ(position.realizedPnL, 0.0);
}

TEST_F(PositionTrackerTest, BuyPosition) {
    tracker->updatePosition("ACC001", "AAPL", Side::BUY, 100, 150.0);
    
    auto position = tracker->getPosition("ACC001", "AAPL");
    
    EXPECT_EQ(position.quantity, 100);
    EXPECT_EQ(position.avgPrice, 150.0);
}

TEST_F(PositionTrackerTest, BuyAndSellSameQuantity) {
    tracker->updatePosition("ACC001", "AAPL", Side::BUY, 100, 150.0);
    tracker->updatePosition("ACC001", "AAPL", Side::SELL, 100, 155.0);
    
    auto position = tracker->getPosition("ACC001", "AAPL");
    
    EXPECT_NEAR(position.quantity, 0.0, 1e-6);
    EXPECT_NEAR(position.realizedPnL, 500.0, 1e-6);
}

TEST_F(PositionTrackerTest, PartialSell) {
    tracker->updatePosition("ACC001", "AAPL", Side::BUY, 100, 150.0);
    tracker->updatePosition("ACC001", "AAPL", Side::SELL, 50, 155.0);
    
    auto position = tracker->getPosition("ACC001", "AAPL");
    
    EXPECT_EQ(position.quantity, 50);
    EXPECT_NEAR(position.realizedPnL, 250.0, 1e-6);
}

TEST_F(PositionTrackerTest, ShortPosition) {
    tracker->updatePosition("ACC001", "AAPL", Side::SELL, 100, 150.0);
    
    auto position = tracker->getPosition("ACC001", "AAPL");
    
    EXPECT_EQ(position.quantity, -100);
}

TEST_F(PositionTrackerTest, MultipleSymbols) {
    tracker->updatePosition("ACC001", "AAPL", Side::BUY, 100, 150.0);
    tracker->updatePosition("ACC001", "GOOGL", Side::BUY, 50, 2800.0);
    
    auto positions = tracker->getPositionsByAccount("ACC001");
    EXPECT_EQ(positions.size(), 2);
}

TEST_F(PositionTrackerTest, TotalExposure) {
    tracker->updatePosition("ACC001", "AAPL", Side::BUY, 100, 150.0);
    tracker->updatePosition("ACC001", "GOOGL", Side::BUY, 50, 2800.0);
    
    double exposure = tracker->getTotalExposure("ACC001");
    EXPECT_NEAR(exposure, 155000.0, 1e-6);
}

TEST_F(PositionTrackerTest, SymbolExposure) {
    tracker->updatePosition("ACC001", "AAPL", Side::BUY, 100, 150.0);
    
    double exposure = tracker->getSymbolExposure("ACC001", "AAPL");
    EXPECT_NEAR(exposure, 15000.0, 1e-6);
}

TEST_F(PositionTrackerTest, ResetAccount) {
    tracker->updatePosition("ACC001", "AAPL", Side::BUY, 100, 150.0);
    tracker->resetAccount("ACC001");
    
    auto position = tracker->getPosition("ACC001", "AAPL");
    EXPECT_EQ(position.quantity, 0.0);
}
