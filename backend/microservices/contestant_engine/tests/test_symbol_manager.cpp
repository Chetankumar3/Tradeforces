#include <gtest/gtest.h>
#include "market_data/symbol_manager.h"
#include <fstream>

using namespace MarketData;

class SymbolManagerTest : public ::testing::Test {
protected:
    SymbolManager manager;
};

TEST_F(SymbolManagerTest, AddSymbol) {
    InstrumentDetails details;
    details.symbol = "AAPL";
    details.instrumentType = "STOCK";
    details.tickSize = 0.01;
    details.lotSize = 1.0;
    details.isActive = true;
    
    EXPECT_TRUE(manager.addSymbol("AAPL", details));
    EXPECT_TRUE(manager.isValidSymbol("AAPL"));
}

TEST_F(SymbolManagerTest, AddDuplicateSymbol) {
    InstrumentDetails details;
    details.symbol = "AAPL";
    
    EXPECT_TRUE(manager.addSymbol("AAPL", details));
    EXPECT_FALSE(manager.addSymbol("AAPL", details));
}

TEST_F(SymbolManagerTest, RemoveSymbol) {
    InstrumentDetails details;
    details.symbol = "AAPL";
    
    manager.addSymbol("AAPL", details);
    EXPECT_TRUE(manager.isValidSymbol("AAPL"));
    
    EXPECT_TRUE(manager.removeSymbol("AAPL"));
    EXPECT_FALSE(manager.isValidSymbol("AAPL"));
}

TEST_F(SymbolManagerTest, IsTradable) {
    InstrumentDetails details;
    details.symbol = "AAPL";
    details.isActive = true;
    
    manager.addSymbol("AAPL", details);
    EXPECT_TRUE(manager.isTradable("AAPL"));
    
    details.isActive = false;
    manager.removeSymbol("AAPL");
    manager.addSymbol("AAPL", details);
    EXPECT_FALSE(manager.isTradable("AAPL"));
}

TEST_F(SymbolManagerTest, RoundToTickSize) {
    InstrumentDetails details;
    details.symbol = "AAPL";
    details.tickSize = 0.05;
    
    manager.addSymbol("AAPL", details);
    
    EXPECT_DOUBLE_EQ(100.0, manager.roundToTickSize("AAPL", 100.01));
    EXPECT_DOUBLE_EQ(100.05, manager.roundToTickSize("AAPL", 100.03));
    EXPECT_DOUBLE_EQ(100.10, manager.roundToTickSize("AAPL", 100.08));
}

TEST_F(SymbolManagerTest, RoundToLotSize) {
    InstrumentDetails details;
    details.symbol = "AAPL";
    details.lotSize = 10.0;
    
    manager.addSymbol("AAPL", details);
    
    EXPECT_DOUBLE_EQ(100.0, manager.roundToLotSize("AAPL", 105.0));
    EXPECT_DOUBLE_EQ(110.0, manager.roundToLotSize("AAPL", 107.0));
}

TEST_F(SymbolManagerTest, IsWithinPriceLimits) {
    InstrumentDetails details;
    details.symbol = "AAPL";
    details.minPrice = 10.0;
    details.maxPrice = 1000.0;
    
    manager.addSymbol("AAPL", details);
    
    EXPECT_TRUE(manager.isWithinPriceLimits("AAPL", 100.0));
    EXPECT_TRUE(manager.isWithinPriceLimits("AAPL", 10.0));
    EXPECT_TRUE(manager.isWithinPriceLimits("AAPL", 1000.0));
    EXPECT_FALSE(manager.isWithinPriceLimits("AAPL", 5.0));
    EXPECT_FALSE(manager.isWithinPriceLimits("AAPL", 2000.0));
}

TEST_F(SymbolManagerTest, GetAllSymbols) {
    InstrumentDetails details1;
    details1.symbol = "AAPL";
    InstrumentDetails details2;
    details2.symbol = "GOOGL";
    
    manager.addSymbol("AAPL", details1);
    manager.addSymbol("GOOGL", details2);
    
    auto symbols = manager.getAllSymbols();
    EXPECT_EQ(2, symbols.size());
}

TEST_F(SymbolManagerTest, GetTradableSymbols) {
    InstrumentDetails active;
    active.symbol = "AAPL";
    active.isActive = true;
    
    InstrumentDetails inactive;
    inactive.symbol = "GOOGL";
    inactive.isActive = false;
    
    manager.addSymbol("AAPL", active);
    manager.addSymbol("GOOGL", inactive);
    
    auto tradable = manager.getTradableSymbols();
    EXPECT_EQ(1, tradable.size());
    EXPECT_EQ("AAPL", tradable[0]);
}
