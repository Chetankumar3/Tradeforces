#include <gtest/gtest.h>
#include "utils/config_loader.h"
#include "utils/logger.h"
#include <fstream>

using namespace fix_gateway::utils;

class ConfigLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::getInstance().initialize("logs");
        
        createTestConfig();
        createTestRules();
    }
    
    void TearDown() override {
        std::remove("test_config.json");
        std::remove("test_rules.json");
    }
    
    void createTestConfig() {
        std::ofstream file("test_config.json");
        file << R"({
            "sessions": [
                {
                    "sessionId": "SESSION1",
                    "senderCompId": "SENDER1",
                    "targetCompId": "TARGET1",
                    "heartbeatInterval": 30,
                    "isInitiator": true,
                    "socketConnectHost": "localhost",
                    "socketConnectPort": 9876
                },
                {
                    "sessionId": "SESSION2",
                    "senderCompId": "SENDER2",
                    "targetCompId": "TARGET2",
                    "heartbeatInterval": 60,
                    "isInitiator": false,
                    "socketAcceptPort": 9877
                }
            ]
        })";
        file.close();
    }
    
    void createTestRules() {
        std::ofstream file("test_rules.json");
        file << R"({
            "maxOrderSize": 500000.0,
            "minOrderSize": 10.0,
            "maxPositionSize": 5000000.0,
            "maxOrdersPerSecond": 50,
            "tradableSymbols": ["AAPL", "GOOGL", "MSFT", "TSLA"]
        })";
        file.close();
    }
};

TEST_F(ConfigLoaderTest, LoadConfigSuccess) {
    auto& loader = ConfigLoader::getInstance();
    
    EXPECT_TRUE(loader.loadConfig("test_config.json"));
}

TEST_F(ConfigLoaderTest, LoadConfigFileNotFound) {
    auto& loader = ConfigLoader::getInstance();
    
    EXPECT_FALSE(loader.loadConfig("nonexistent.json"));
}

TEST_F(ConfigLoaderTest, GetSessions) {
    auto& loader = ConfigLoader::getInstance();
    loader.loadConfig("test_config.json");
    
    auto sessions = loader.getSessions();
    
    EXPECT_EQ(sessions.size(), 2);
}

TEST_F(ConfigLoaderTest, GetSessionById) {
    auto& loader = ConfigLoader::getInstance();
    loader.loadConfig("test_config.json");
    
    auto session = loader.getSession("SESSION1");
    
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->sessionId, "SESSION1");
    EXPECT_EQ(session->senderCompId, "SENDER1");
    EXPECT_EQ(session->targetCompId, "TARGET1");
    EXPECT_EQ(session->heartbeatInterval, 30);
    EXPECT_TRUE(session->isInitiator);
}

TEST_F(ConfigLoaderTest, GetSessionNotFound) {
    auto& loader = ConfigLoader::getInstance();
    loader.loadConfig("test_config.json");
    
    auto session = loader.getSession("NONEXISTENT");
    
    EXPECT_FALSE(session.has_value());
}

TEST_F(ConfigLoaderTest, SessionConfigParsing) {
    auto& loader = ConfigLoader::getInstance();
    loader.loadConfig("test_config.json");
    
    auto session = loader.getSession("SESSION2");
    
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->sessionId, "SESSION2");
    EXPECT_EQ(session->heartbeatInterval, 60);
    EXPECT_FALSE(session->isInitiator);
    EXPECT_EQ(session->socketAcceptPort, 9877);
}

TEST_F(ConfigLoaderTest, LoadTradingRulesSuccess) {
    auto& loader = ConfigLoader::getInstance();
    
    EXPECT_TRUE(loader.loadTradingRules("test_rules.json"));
}

TEST_F(ConfigLoaderTest, GetTradingRules) {
    auto& loader = ConfigLoader::getInstance();
    loader.loadTradingRules("test_rules.json");
    
    auto rules = loader.getTradingRules();
    
    EXPECT_DOUBLE_EQ(rules.maxOrderSize, 500000.0);
    EXPECT_DOUBLE_EQ(rules.minOrderSize, 10.0);
    EXPECT_DOUBLE_EQ(rules.maxPositionSize, 5000000.0);
    EXPECT_EQ(rules.maxOrdersPerSecond, 50);
    EXPECT_EQ(rules.tradableSymbols.size(), 4);
    EXPECT_EQ(rules.tradableSymbols[0], "AAPL");
}

TEST_F(ConfigLoaderTest, TradableSymbolsList) {
    auto& loader = ConfigLoader::getInstance();
    loader.loadTradingRules("test_rules.json");
    
    auto rules = loader.getTradingRules();
    
    EXPECT_TRUE(std::find(rules.tradableSymbols.begin(), rules.tradableSymbols.end(), "AAPL") != rules.tradableSymbols.end());
    EXPECT_TRUE(std::find(rules.tradableSymbols.begin(), rules.tradableSymbols.end(), "GOOGL") != rules.tradableSymbols.end());
    EXPECT_TRUE(std::find(rules.tradableSymbols.begin(), rules.tradableSymbols.end(), "MSFT") != rules.tradableSymbols.end());
    EXPECT_TRUE(std::find(rules.tradableSymbols.begin(), rules.tradableSymbols.end(), "TSLA") != rules.tradableSymbols.end());
}
