#include <gtest/gtest.h>
#include "fix_engine/fix_message.h"

using namespace fix_gateway::fix_engine;

class FIXMessageTest : public ::testing::Test {
protected:
    void SetUp() override {
        message = std::make_unique<FIXMessage>(MsgType::NEW_ORDER_SINGLE);
    }
    
    std::unique_ptr<FIXMessage> message;
};

TEST_F(FIXMessageTest, SetAndGetField) {
    message->setField(11, "ORDER123");
    auto value = message->getField(11);
    
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "ORDER123");
}

TEST_F(FIXMessageTest, HasField) {
    message->setField(55, "AAPL");
    
    EXPECT_TRUE(message->hasField(55));
    EXPECT_FALSE(message->hasField(100));
}

TEST_F(FIXMessageTest, RemoveField) {
    message->setField(38, "1000");
    EXPECT_TRUE(message->hasField(38));
    
    message->removeField(38);
    EXPECT_FALSE(message->hasField(38));
}

TEST_F(FIXMessageTest, SetHeader) {
    message->setHeader("SENDER", "TARGET", 42);
    
    EXPECT_EQ(message->getSenderCompId(), "SENDER");
    EXPECT_EQ(message->getTargetCompId(), "TARGET");
    EXPECT_EQ(message->getMsgSeqNum(), 42);
}

TEST_F(FIXMessageTest, MsgTypeHandling) {
    EXPECT_EQ(message->getMsgType(), MsgType::NEW_ORDER_SINGLE);
    
    message->setMsgType(MsgType::EXECUTION_REPORT);
    EXPECT_EQ(message->getMsgType(), MsgType::EXECUTION_REPORT);
}

TEST_F(FIXMessageTest, SerializeBasicMessage) {
    message->setHeader("SENDER", "TARGET", 1);
    message->setField(11, "ORDER123");
    message->setField(55, "AAPL");
    
    std::string serialized = message->serialize();
    
    EXPECT_FALSE(serialized.empty());
    EXPECT_NE(serialized.find("8=FIX.4.4"), std::string::npos);
    EXPECT_NE(serialized.find("35=D"), std::string::npos);
}

TEST_F(FIXMessageTest, ChecksumCalculation) {
    message->setHeader("SENDER", "TARGET", 1);
    
    std::string checksum = message->calculateChecksum();
    
    EXPECT_EQ(checksum.length(), 3);
    EXPECT_TRUE(std::all_of(checksum.begin(), checksum.end(), ::isdigit));
}

TEST_F(FIXMessageTest, ValidationWithRequiredFields) {
    message->setField(8, "FIX.4.4");
    message->setField(9, "100");
    message->setField(35, "D");
    message->setField(49, "SENDER");
    message->setField(56, "TARGET");
    message->setField(34, "1");
    message->setField(52, "20231201-12:00:00.000");
    
    EXPECT_TRUE(message->validate());
}

TEST_F(FIXMessageTest, ValidationFailsWithoutRequiredFields) {
    EXPECT_FALSE(message->validate());
}

TEST_F(FIXMessageTest, TimestampHandling) {
    auto now = std::chrono::system_clock::now();
    message->setTimestamp(now);
    
    auto retrieved = message->getTimestamp();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(retrieved - now);
    
    EXPECT_LT(std::abs(diff.count()), 10);
}
