#include <gtest/gtest.h>
#include "market_data/order_book_level.h"
#include <memory>

using namespace MarketData;
using namespace fix_gateway::order_manager;

class OrderBookLevelTest : public ::testing::Test {
protected:
    OrderBookLevel book{"AAPL"};
    
    std::shared_ptr<Order> createOrder(const std::string& id, Side side, 
                                              double price, double qty) {
        auto order = std::make_shared<Order>();
        order->orderId = id;
        order->clOrdId = "CL" + id;
        order->symbol = "AAPL";
        order->side = side;
        order->orderType = OrderType::LIMIT;
        order->price = price;
        order->orderQty = qty;
        order->cumQty = 0.0;
        order->state = OrderState::NEW;
        return order;
    }
};

TEST_F(OrderBookLevelTest, AddBuyOrder) {
    auto order = createOrder("1", Side::BUY, 100.0, 100.0);
    book.addOrder(order);
    
    EXPECT_DOUBLE_EQ(100.0, book.getBestBid());
    EXPECT_EQ(1, book.getBidDepth());
}

TEST_F(OrderBookLevelTest, AddSellOrder) {
    auto order = createOrder("1", Side::SELL, 101.0, 100.0);
    book.addOrder(order);
    
    EXPECT_DOUBLE_EQ(101.0, book.getBestAsk());
    EXPECT_EQ(1, book.getAskDepth());
}

TEST_F(OrderBookLevelTest, BestBidAsk) {
    book.addOrder(createOrder("1", Side::BUY, 99.0, 100.0));
    book.addOrder(createOrder("2", Side::BUY, 100.0, 100.0));
    book.addOrder(createOrder("3", Side::SELL, 101.0, 100.0));
    book.addOrder(createOrder("4", Side::SELL, 102.0, 100.0));
    
    EXPECT_DOUBLE_EQ(100.0, book.getBestBid());
    EXPECT_DOUBLE_EQ(101.0, book.getBestAsk());
}

TEST_F(OrderBookLevelTest, MidPrice) {
    book.addOrder(createOrder("1", Side::BUY, 100.0, 100.0));
    book.addOrder(createOrder("2", Side::SELL, 102.0, 100.0));
    
    EXPECT_DOUBLE_EQ(101.0, book.getMidPrice());
}

TEST_F(OrderBookLevelTest, Spread) {
    book.addOrder(createOrder("1", Side::BUY, 100.0, 100.0));
    book.addOrder(createOrder("2", Side::SELL, 105.0, 100.0));
    
    EXPECT_DOUBLE_EQ(5.0, book.getSpread());
}

TEST_F(OrderBookLevelTest, RemoveOrder) {
    auto order = createOrder("1", Side::BUY, 100.0, 100.0);
    book.addOrder(order);
    
    EXPECT_EQ(1, book.getBidDepth());
    
    book.removeOrder("1", Side::BUY);
    
    EXPECT_EQ(0, book.getBidDepth());
    EXPECT_DOUBLE_EQ(0.0, book.getBestBid());
}

TEST_F(OrderBookLevelTest, GetBidLevels) {
    book.addOrder(createOrder("1", Side::BUY, 100.0, 100.0));
    book.addOrder(createOrder("2", Side::BUY, 99.0, 200.0));
    book.addOrder(createOrder("3", Side::BUY, 98.0, 150.0));
    
    auto levels = book.getBidLevels(3);
    
    ASSERT_EQ(3, levels.size());
    EXPECT_DOUBLE_EQ(100.0, levels[0].first);
    EXPECT_DOUBLE_EQ(99.0, levels[1].first);
    EXPECT_DOUBLE_EQ(98.0, levels[2].first);
}

TEST_F(OrderBookLevelTest, GetAskLevels) {
    book.addOrder(createOrder("1", Side::SELL, 101.0, 100.0));
    book.addOrder(createOrder("2", Side::SELL, 102.0, 200.0));
    book.addOrder(createOrder("3", Side::SELL, 103.0, 150.0));
    
    auto levels = book.getAskLevels(3);
    
    ASSERT_EQ(3, levels.size());
    EXPECT_DOUBLE_EQ(101.0, levels[0].first);
    EXPECT_DOUBLE_EQ(102.0, levels[1].first);
    EXPECT_DOUBLE_EQ(103.0, levels[2].first);
}

TEST_F(OrderBookLevelTest, PriceTimePriority) {
    auto order1 = createOrder("1", Side::BUY, 100.0, 100.0);
    auto order2 = createOrder("2", Side::BUY, 100.0, 200.0);
    
    book.addOrder(order1);
    book.addOrder(order2);
    
    auto bestOrder = book.getBestBidOrder();
    EXPECT_EQ("1", bestOrder->orderId);
}
