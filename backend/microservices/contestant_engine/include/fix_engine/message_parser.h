#pragma once

#include "fix_message.h"
#include <string>
#include <memory>

namespace fix_gateway {
namespace fix_engine {

class MessageParser {
public:
    static std::unique_ptr<FIXMessage> parse(const std::string& rawMessage);
    static std::string serialize(const FIXMessage& message);
    
    static bool validateFormat(const std::string& rawMessage);
    static std::string extractField(const std::string& rawMessage, int tag);
    
private:
    static constexpr char SOH = '\x01';
    static constexpr int TAG_BEGIN_STRING = 8;
    static constexpr int TAG_BODY_LENGTH = 9;
    static constexpr int TAG_MSG_TYPE = 35;
    static constexpr int TAG_CHECKSUM = 10;
    static constexpr int TAG_MSG_SEQ_NUM = 34;
    static constexpr int TAG_SENDER_COMP_ID = 49;
    static constexpr int TAG_TARGET_COMP_ID = 56;
    static constexpr int TAG_SENDING_TIME = 52;
    
    static std::string calculateChecksum(const std::string& message);
    static int extractBodyLength(const std::string& rawMessage);
};

}
}
