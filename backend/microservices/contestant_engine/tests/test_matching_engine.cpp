#include <gtest/gtest.h>
#include "market_data/matching_engine.h"
#include "market_data/order_book_level.h"
#include "market_data/price_generator.h"
#include <memory>

using namespace MarketData;
using namespace fix_gateway::order_manager;

class MatchingEngineTest : public ::testing::Test {
protected:
    PriceGenerator priceGen;
    MatchingEngine engine{priceGen};
    
    std::vector<Fill> fills;
    std::vector<Trade> trades;
    
    void SetUp() override {
        PriceConfig config(100.0, 0.02, 0.0, 1.0, 10000.0, 10.0);
        priceGen.addSymbol("AAPL", config);
        
        engine.setFillCallback([this](const Fill& fill) {
            fills.push_back(fill);
        });
        
        engine.setTradeCallback([this](const Trade& trade) {
            trades.push_back(trade);
        });
    }
    
    std::shared_ptr<Order> createOrder(const std::string& id, Side side,
                                              OrderType type, double price, double qty) {
        auto order = std::make_shared<Order>();
        order->orderId = id;
        order->clOrdId = "CL" + id;
        order->symbol = "AAPL";
        order->side = side;
        order->orderType = type;
        order->price = price;
        order->orderQty = qty;
        order->cumQty = 0.0;
        order->state = OrderState::NEW;
        return order;
    }
};

TEST_F(MatchingEngineTest, MarketBuyAgainstLimit) {
    auto sellOrder = createOrder("1", Side::SELL, OrderType::LIMIT, 100.0, 100.0);
    engine.submitOrder(sellOrder);
    
    auto buyOrder = createOrder("2", Side::BUY, OrderType::MARKET, 0.0, 50.0);
    engine.submitOrder(buyOrder);
    
    EXPECT_EQ(1, fills.size());
    EXPECT_DOUBLE_EQ(50.0, fills[0].quantity);
    EXPECT_DOUBLE_EQ(100.0, fills[0].price);
    EXPECT_DOUBLE_EQ(50.0, buyOrder->cumQty);
    EXPECT_EQ(OrderState::FILLED, buyOrder->state);
}

TEST_F(MatchingEngineTest, LimitBuyAgainstLimitSell) {
    auto sellOrder = createOrder("1", Side::SELL, OrderType::LIMIT, 100.0, 100.0);
    engine.submitOrder(sellOrder);
    
    auto buyOrder = createOrder("2", Side::BUY, OrderType::LIMIT, 100.0, 75.0);
    engine.submitOrder(buyOrder);
    
    EXPECT_EQ(1, fills.size());
    EXPECT_DOUBLE_EQ(75.0, fills[0].quantity);
    EXPECT_DOUBLE_EQ(100.0, fills[0].price);
}

TEST_F(MatchingEngineTest, PartialFill) {
    auto sellOrder = createOrder("1", Side::SELL, OrderType::LIMIT, 100.0, 100.0);
    engine.submitOrder(sellOrder);
    
    auto buyOrder = createOrder("2", Side::BUY, OrderType::MARKET, 0.0, 150.0);
    engine.submitOrder(buyOrder);
    
    EXPECT_EQ(1, fills.size());
    EXPECT_DOUBLE_EQ(100.0, fills[0].quantity);
    EXPECT_DOUBLE_EQ(100.0, buyOrder->cumQty);
    EXPECT_EQ(OrderState::PARTIALLY_FILLED, buyOrder->state);
}

TEST_F(MatchingEngineTest, MultipleFills) {
    engine.submitOrder(createOrder("1", Side::SELL, OrderType::LIMIT, 100.0, 50.0));
    engine.submitOrder(createOrder("2", Side::SELL, OrderType::LIMIT, 100.5, 75.0));
    
    auto buyOrder = createOrder("3", Side::BUY, OrderType::MARKET, 0.0, 100.0);
    engine.submitOrder(buyOrder);
    
    EXPECT_EQ(2, fills.size());
    EXPECT_DOUBLE_EQ(100.0, buyOrder->cumQty);
}

TEST_F(MatchingEngineTest, NoMatchPriceNotCrossed) {
    auto sellOrder = createOrder("1", Side::SELL, OrderType::LIMIT, 102.0, 100.0);
    engine.submitOrder(sellOrder);
    
    auto buyOrder = createOrder("2", Side::BUY, OrderType::LIMIT, 100.0, 100.0);
    engine.submitOrder(buyOrder);
    
    EXPECT_EQ(0, fills.size());
    EXPECT_DOUBLE_EQ(0.0, buyOrder->cumQty);
    EXPECT_EQ(OrderState::NEW, buyOrder->state);
}

TEST_F(MatchingEngineTest, PriceTimePriority) {
    engine.submitOrder(createOrder("1", Side::SELL, OrderType::LIMIT, 100.0, 50.0));
    engine.submitOrder(createOrder("2", Side::SELL, OrderType::LIMIT, 100.0, 75.0));
    
    auto buyOrder = createOrder("3", Side::BUY, OrderType::MARKET, 0.0, 60.0);
    engine.submitOrder(buyOrder);
    
    ASSERT_EQ(2, fills.size());
    EXPECT_EQ("1", fills[0].sellOrderId);
    EXPECT_DOUBLE_EQ(50.0, fills[0].quantity);
    EXPECT_EQ("2", fills[1].sellOrderId);
    EXPECT_DOUBLE_EQ(10.0, fills[1].quantity);
}

TEST_F(MatchingEngineTest, CancelOrder) {
    auto order = createOrder("1", Side::BUY, OrderType::LIMIT, 100.0, 100.0);
    engine.submitOrder(order);
    
    auto book = engine.getOrderBook("AAPL");
    EXPECT_EQ(1, book->getBidDepth());
    
    engine.cancelOrder("1", "AAPL");
    EXPECT_EQ(0, book->getBidDepth());
}

TEST_F(MatchingEngineTest, TradePublication) {
    engine.submitOrder(createOrder("1", Side::SELL, OrderType::LIMIT, 100.0, 100.0));
    engine.submitOrder(createOrder("2", Side::BUY, OrderType::MARKET, 0.0, 100.0));
    
    EXPECT_EQ(1, trades.size());
    EXPECT_EQ("AAPL", trades[0].symbol);
    EXPECT_DOUBLE_EQ(100.0, trades[0].price);
    EXPECT_DOUBLE_EQ(100.0, trades[0].quantity);
}

TEST_F(MatchingEngineTest, AveragePriceCalculation) {
    engine.submitOrder(createOrder("1", Side::SELL, OrderType::LIMIT, 100.0, 50.0));
    engine.submitOrder(createOrder("2", Side::SELL, OrderType::LIMIT, 101.0, 50.0));
    
    auto buyOrder = createOrder("3", Side::BUY, OrderType::MARKET, 0.0, 100.0);
    engine.submitOrder(buyOrder);
    
    EXPECT_DOUBLE_EQ(100.5, buyOrder->avgPx);
}
