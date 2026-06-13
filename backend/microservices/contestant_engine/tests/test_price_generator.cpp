#include <gtest/gtest.h>
#include "market_data/price_generator.h"

using namespace MarketData;

class PriceGeneratorTest : public ::testing::Test {
protected:
    PriceGenerator generator;
};

TEST_F(PriceGeneratorTest, AddSymbol) {
    PriceConfig config(100.0, 0.02, 0.0, 1.0, 10000.0, 10.0);
    generator.addSymbol("AAPL", config);
    
    double midPrice = generator.getMidPrice("AAPL");
    EXPECT_DOUBLE_EQ(100.0, midPrice);
}

TEST_F(PriceGeneratorTest, BidAskSpread) {
    PriceConfig config(100.0, 0.02, 0.0, 1.0, 10000.0, 10.0);
    generator.addSymbol("AAPL", config);
    
    double bidPrice = generator.getBidPrice("AAPL");
    double askPrice = generator.getAskPrice("AAPL");
    double spread = generator.getSpread("AAPL");
    
    EXPECT_LT(bidPrice, askPrice);
    EXPECT_GT(spread, 0.0);
    EXPECT_DOUBLE_EQ(spread, askPrice - bidPrice);
}

TEST_F(PriceGeneratorTest, UpdatePrice) {
    PriceConfig config(100.0, 0.02, 0.0, 1.0, 10000.0, 10.0);
    generator.addSymbol("AAPL", config);
    
    double initialPrice = generator.getMidPrice("AAPL");
    generator.updatePrice("AAPL");
    double updatedPrice = generator.getMidPrice("AAPL");
    
    EXPECT_GT(updatedPrice, 0.0);
}

TEST_F(PriceGeneratorTest, PriceLimits) {
    PriceConfig config(5.0, 0.5, 0.0, 1.0, 10.0, 10.0);
    generator.addSymbol("TEST", config);
    
    for (int i = 0; i < 1000; ++i) {
        generator.updatePrice("TEST");
        double price = generator.getMidPrice("TEST");
        EXPECT_GE(price, 1.0);
        EXPECT_LE(price, 10.0);
    }
}

TEST_F(PriceGeneratorTest, SetLastTradePrice) {
    PriceConfig config(100.0, 0.02, 0.0, 1.0, 10000.0, 10.0);
    generator.addSymbol("AAPL", config);
    
    generator.setLastTradePrice("AAPL", 105.50);
    double midPrice = generator.getMidPrice("AAPL");
    
    EXPECT_DOUBLE_EQ(105.50, midPrice);
}

TEST_F(PriceGeneratorTest, MultipleSymbols) {
    PriceConfig config1(100.0, 0.02, 0.0, 1.0, 10000.0, 10.0);
    PriceConfig config2(200.0, 0.03, 0.0, 1.0, 10000.0, 15.0);
    
    generator.addSymbol("AAPL", config1);
    generator.addSymbol("GOOGL", config2);
    
    EXPECT_DOUBLE_EQ(100.0, generator.getMidPrice("AAPL"));
    EXPECT_DOUBLE_EQ(200.0, generator.getMidPrice("GOOGL"));
}

TEST_F(PriceGeneratorTest, UnknownSymbol) {
    double price = generator.getMidPrice("UNKNOWN");
    EXPECT_DOUBLE_EQ(0.0, price);
}
