#include "fix_engine/message_parser.h"
#include <sstream>
#include <iomanip>

namespace fix_gateway {
namespace fix_engine {

std::unique_ptr<FIXMessage> MessageParser::parse(const std::string& rawMessage) {
    if (!validateFormat(rawMessage)) {
        return nullptr;
    }
    
    auto message = std::make_unique<FIXMessage>();
    
    if (!message->parse(rawMessage)) {
        return nullptr;
    }
    
    return message;
}

std::string MessageParser::serialize(const FIXMessage& message) {
    return message.serialize();
}

bool MessageParser::validateFormat(const std::string& rawMessage) {
    if (rawMessage.empty()) {
        return false;
    }
    
    if (rawMessage.substr(0, 2) != "8=") {
        return false;
    }
    
    size_t firstSOH = rawMessage.find(SOH);
    if (firstSOH == std::string::npos) {
        return false;
    }
    
    std::string beginString = rawMessage.substr(2, firstSOH - 2);
    if (beginString != "FIX.4.4" && beginString != "FIX.4.2" && beginString != "FIX.5.0") {
        return false;
    }
    
    size_t bodyLengthPos = rawMessage.find("9=", firstSOH);
    if (bodyLengthPos == std::string::npos) {
        return false;
    }
    
    size_t checksumPos = rawMessage.rfind("10=");
    if (checksumPos == std::string::npos) {
        return false;
    }
    
    return true;
}

std::string MessageParser::extractField(const std::string& rawMessage, int tag) {
    std::string tagStr = std::to_string(tag) + "=";
    size_t tagPos = rawMessage.find(tagStr);
    
    if (tagPos == std::string::npos) {
        return "";
    }
    
    size_t valueStart = tagPos + tagStr.length();
    size_t sohPos = rawMessage.find(SOH, valueStart);
    
    if (sohPos == std::string::npos) {
        return "";
    }
    
    return rawMessage.substr(valueStart, sohPos - valueStart);
}

std::string MessageParser::calculateChecksum(const std::string& message) {
    int sum = 0;
    for (char c : message) {
        sum += static_cast<unsigned char>(c);
    }
    sum %= 256;
    
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(3) << sum;
    return oss.str();
}

int MessageParser::extractBodyLength(const std::string& rawMessage) {
    std::string bodyLengthStr = extractField(rawMessage, TAG_BODY_LENGTH);
    if (bodyLengthStr.empty()) {
        return -1;
    }
    
    try {
        return std::stoi(bodyLengthStr);
    } catch (...) {
        return -1;
    }
}

}
}
