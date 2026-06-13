#include <gtest/gtest.h>
#include "order_manager/order_validator.h"
#include "utils/config_loader.h"

using namespace fix_gateway::order_manager;
using namespace fix_gateway::utils;

class OrderValidatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        TradingRules rules;
        rules.minOrderSize = 10.0;
        rules.maxOrderSize = 10000.0;
        rules.tradableSymbols = {"AAPL", "GOOGL", "MSFT"};
        
        validator = std::make_unique<OrderValidator>(rules);
        
        order.clOrdId = "CL001";
        order.symbol = "AAPL";
        order.side = Side::BUY;
        order.orderType = OrderType::LIMIT;
        order.orderQty = 100;
        order.price = 150.0;
    }
    
    std::unique_ptr<OrderValidator> validator;
    Order order;
};

TEST_F(OrderValidatorTest, ValidOrder) {
    auto result = validator->validate(order);
    EXPECT_EQ(result, ValidationResult::VALID);
}

TEST_F(OrderValidatorTest, InvalidSymbol) {
    order.symbol = "INVALID";
    auto result = validator->validate(order);
    EXPECT_EQ(result, ValidationResult::INVALID_SYMBOL);
}

TEST_F(OrderValidatorTest, QuantityTooSmall) {
    order.orderQty = 5.0;
    auto result = validator->validate(order);
    EXPECT_EQ(result, ValidationResult::QUANTITY_TOO_SMALL);
}

TEST_F(OrderValidatorTest, QuantityTooLarge) {
    order.orderQty = 20000.0;
    auto result = validator->validate(order);
    EXPECT_EQ(result, ValidationResult::QUANTITY_TOO_LARGE);
}

TEST_F(OrderValidatorTest, InvalidPrice) {
    order.price = -10.0;
    auto result = validator->validate(order);
    EXPECT_EQ(result, ValidationResult::INVALID_PRICE);
}

TEST_F(OrderValidatorTest, MarketOrderValid) {
    order.orderType = OrderType::MARKET;
    order.price = 0.0;
    auto result = validator->validate(order);
    EXPECT_EQ(result, ValidationResult::VALID);
}

TEST_F(OrderValidatorTest, DuplicateOrder) {
    validator->validate(order);
    validator->markOrderProcessed(order.clOrdId);
    
    auto result = validator->validate(order);
    EXPECT_EQ(result, ValidationResult::DUPLICATE_ORDER);
}

TEST_F(OrderValidatorTest, ValidationMessage) {
    std::string msg = validator->getValidationMessage(ValidationResult::INVALID_SYMBOL);
    EXPECT_FALSE(msg.empty());
}

TEST_F(OrderValidatorTest, StopOrderWithPrice) {
    order.orderType = OrderType::STOP;
    order.stopPx = 145.0;
    
    auto result = validator->validate(order);
    EXPECT_EQ(result, ValidationResult::VALID);
}

TEST_F(OrderValidatorTest, StopOrderInvalidPrice) {
    order.orderType = OrderType::STOP;
    order.stopPx = -10.0;
    
    auto result = validator->validate(order);
    EXPECT_EQ(result, ValidationResult::INVALID_STOP_PRICE);
}
