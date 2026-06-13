#include <gtest/gtest.h>
#include "fix_engine/message_parser.h"

using namespace fix_gateway::fix_engine;

class MessageParserTest : public ::testing::Test {
protected:
    static constexpr char SOH = '\x01';
    
    std::string createRawMessage(const std::string& msgType, const std::vector<std::pair<int, std::string>>& fields) {
        std::ostringstream oss;
        oss << "8=FIX.4.4" << SOH;
        
        std::ostringstream body;
        body << "35=" << msgType << SOH;
        body << "49=SENDER" << SOH;
        body << "56=TARGET" << SOH;
        body << "34=1" << SOH;
        body << "52=20231201-12:00:00.000" << SOH;
        
        for (const auto& [tag, value] : fields) {
            body << tag << "=" << value << SOH;
        }
        
        std::string bodyStr = body.str();
        oss << "9=" << bodyStr.length() << SOH;
        oss << bodyStr;
        oss << "10=000" << SOH;
        
        return oss.str();
    }
};

TEST_F(MessageParserTest, ParseValidMessage) {
    std::string rawMsg = createRawMessage("D", {{11, "ORDER123"}, {55, "AAPL"}});
    
    auto message = MessageParser::parse(rawMsg);
    
    ASSERT_NE(message, nullptr);
    EXPECT_EQ(message->getMsgType(), MsgType::NEW_ORDER_SINGLE);
}

TEST_F(MessageParserTest, ParseInvalidMessage) {
    std::string rawMsg = "invalid message";
    
    auto message = MessageParser::parse(rawMsg);
    
    EXPECT_EQ(message, nullptr);
}

TEST_F(MessageParserTest, ValidateFormatSuccess) {
    std::string rawMsg = createRawMessage("A", {});
    
    EXPECT_TRUE(MessageParser::validateFormat(rawMsg));
}

TEST_F(MessageParserTest, ValidateFormatFailureNoBeginString) {
    std::string rawMsg = "35=A" + std::string(1, SOH);
    
    EXPECT_FALSE(MessageParser::validateFormat(rawMsg));
}

TEST_F(MessageParserTest, ValidateFormatFailureEmpty) {
    std::string rawMsg = "";
    
    EXPECT_FALSE(MessageParser::validateFormat(rawMsg));
}

TEST_F(MessageParserTest, ExtractFieldSuccess) {
    std::string rawMsg = createRawMessage("D", {{11, "ORDER123"}, {55, "AAPL"}});
    
    std::string clOrdId = MessageParser::extractField(rawMsg, 11);
    
    EXPECT_EQ(clOrdId, "ORDER123");
}

TEST_F(MessageParserTest, ExtractFieldNotFound) {
    std::string rawMsg = createRawMessage("D", {{11, "ORDER123"}});
    
    std::string value = MessageParser::extractField(rawMsg, 999);
    
    EXPECT_TRUE(value.empty());
}

TEST_F(MessageParserTest, SerializeMessage) {
    FIXMessage message(MsgType::HEARTBEAT);
    message.setHeader("SENDER", "TARGET", 1);
    
    std::string serialized = MessageParser::serialize(message);
    
    EXPECT_FALSE(serialized.empty());
    EXPECT_NE(serialized.find("8=FIX.4.4"), std::string::npos);
    EXPECT_NE(serialized.find("35=0"), std::string::npos);
}

TEST_F(MessageParserTest, ParseLogonMessage) {
    std::string rawMsg = createRawMessage("A", {{98, "0"}, {108, "30"}});
    
    auto message = MessageParser::parse(rawMsg);
    
    ASSERT_NE(message, nullptr);
    EXPECT_EQ(message->getMsgType(), MsgType::LOGON);
    
    auto heartbeatInt = message->getField(108);
    ASSERT_TRUE(heartbeatInt.has_value());
    EXPECT_EQ(*heartbeatInt, "30");
}

TEST_F(MessageParserTest, ParseLogoutMessage) {
    std::string rawMsg = createRawMessage("5", {{58, "Session ended"}});
    
    auto message = MessageParser::parse(rawMsg);
    
    ASSERT_NE(message, nullptr);
    EXPECT_EQ(message->getMsgType(), MsgType::LOGOUT);
    
    auto text = message->getField(58);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "Session ended");
}
