#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <chrono>

namespace fix_gateway {
namespace fix_engine {

enum class MsgType {
    HEARTBEAT = '0',
    TEST_REQUEST = '1',
    RESEND_REQUEST = '2',
    REJECT = '3',
    SEQUENCE_RESET = '4',
    LOGOUT = '5',
    LOGON = 'A',
    NEW_ORDER_SINGLE = 'D',
    ORDER_CANCEL_REQUEST = 'F',
    ORDER_CANCEL_REPLACE_REQUEST = 'G',
    EXECUTION_REPORT = '8',
    MARKET_DATA_REQUEST = 'V',
    MARKET_DATA_SNAPSHOT = 'W',
    MARKET_DATA_INCREMENTAL_REFRESH = 'X'
};

class FIXMessage {
public:
    FIXMessage() = default;
    explicit FIXMessage(MsgType msgType);
    
    void setField(int tag, const std::string& value);
    std::optional<std::string> getField(int tag) const;
    bool hasField(int tag) const;
    void removeField(int tag);
    
    void setMsgType(MsgType msgType);
    MsgType getMsgType() const;
    
    void setHeader(const std::string& senderCompId, const std::string& targetCompId, int msgSeqNum);
    
    std::string serialize() const;
    bool parse(const std::string& rawMessage);
    
    bool validate() const;
    std::string calculateChecksum() const;
    
    int getMsgSeqNum() const;
    std::string getSenderCompId() const;
    std::string getTargetCompId() const;
    
    std::chrono::system_clock::time_point getTimestamp() const;
    void setTimestamp(std::chrono::system_clock::time_point ts);

private:
    MsgType msgType_;
    std::unordered_map<int, std::string> fields_;
    std::chrono::system_clock::time_point timestamp_;
    
    std::string formatTimestamp() const;
    bool validateChecksum(const std::string& rawMessage) const;
    int calculateBodyLength() const;
};

}
}
