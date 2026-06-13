#include <gtest/gtest.h>
#include "order_manager/order_book.h"

using namespace fix_gateway::order_manager;

class OrderBookTest : public ::testing::Test {
protected:
    void SetUp() override {
        orderBook = std::make_unique<OrderBook>();
        
        order1 = std::make_shared<Order>();
        order1->orderId = "ORD001";
        order1->clOrdId = "CL001";
        order1->symbol = "AAPL";
        order1->account = "ACC001";
        order1->side = Side::BUY;
        order1->orderQty = 100;
        order1->state = OrderState::NEW;
        
        order2 = std::make_shared<Order>();
        order2->orderId = "ORD002";
        order2->clOrdId = "CL002";
        order2->symbol = "AAPL";
        order2->account = "ACC001";
        order2->side = Side::SELL;
        order2->orderQty = 50;
        order2->state = OrderState::NEW;
    }
    
    std::unique_ptr<OrderBook> orderBook;
    std::shared_ptr<Order> order1;
    std::shared_ptr<Order> order2;
};

TEST_F(OrderBookTest, AddOrder) {
    orderBook->addOrder(order1);
    
    EXPECT_EQ(orderBook->getOrderCount(), 1);
    
    auto retrieved = orderBook->getOrder("ORD001");
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->orderId, "ORD001");
}

TEST_F(OrderBookTest, GetOrderByClOrdId) {
    orderBook->addOrder(order1);
    
    auto retrieved = orderBook->getOrderByClOrdId("CL001");
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->clOrdId, "CL001");
}

TEST_F(OrderBookTest, RemoveOrder) {
    orderBook->addOrder(order1);
    EXPECT_EQ(orderBook->getOrderCount(), 1);
    
    bool removed = orderBook->removeOrder("ORD001");
    EXPECT_TRUE(removed);
    EXPECT_EQ(orderBook->getOrderCount(), 0);
}

TEST_F(OrderBookTest, UpdateOrder) {
    orderBook->addOrder(order1);
    
    order1->orderQty = 200;
    bool updated = orderBook->updateOrder(order1);
    
    EXPECT_TRUE(updated);
    
    auto retrieved = orderBook->getOrder("ORD001");
    EXPECT_EQ(retrieved->orderQty, 200);
}

TEST_F(OrderBookTest, GetOrdersBySymbol) {
    orderBook->addOrder(order1);
    orderBook->addOrder(order2);
    
    auto orders = orderBook->getOrdersBySymbol("AAPL");
    EXPECT_EQ(orders.size(), 2);
}

TEST_F(OrderBookTest, GetActiveOrders) {
    orderBook->addOrder(order1);
    orderBook->addOrder(order2);
    
    order2->state = OrderState::FILLED;
    orderBook->updateOrder(order2);
    
    auto activeOrders = orderBook->getActiveOrders();
    EXPECT_EQ(activeOrders.size(), 1);
    EXPECT_EQ(activeOrders[0]->orderId, "ORD001");
}

TEST_F(OrderBookTest, GetOrdersByAccount) {
    orderBook->addOrder(order1);
    orderBook->addOrder(order2);
    
    auto orders = orderBook->getOrdersByAccount("ACC001");
    EXPECT_EQ(orders.size(), 2);
}

TEST_F(OrderBookTest, ClearOrders) {
    orderBook->addOrder(order1);
    orderBook->addOrder(order2);
    
    EXPECT_EQ(orderBook->getOrderCount(), 2);
    
    orderBook->clearOrders();
    EXPECT_EQ(orderBook->getOrderCount(), 0);
}
