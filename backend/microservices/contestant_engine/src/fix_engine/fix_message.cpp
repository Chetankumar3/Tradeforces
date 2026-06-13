#include "fix_engine/fix_message.h"
#include <sstream>
#include <iomanip>
#include <ctime>

namespace fix_gateway {
namespace fix_engine {

FIXMessage::FIXMessage(MsgType msgType) 
    : msgType_(msgType)
    , timestamp_(std::chrono::system_clock::now()) {
}

void FIXMessage::setField(int tag, const std::string& value) {
    fields_[tag] = value;
}

std::optional<std::string> FIXMessage::getField(int tag) const {
    auto it = fields_.find(tag);
    if (it != fields_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool FIXMessage::hasField(int tag) const {
    return fields_.find(tag) != fields_.end();
}

void FIXMessage::removeField(int tag) {
    fields_.erase(tag);
}

void FIXMessage::setMsgType(MsgType msgType) {
    msgType_ = msgType;
}

MsgType FIXMessage::getMsgType() const {
    return msgType_;
}

void FIXMessage::setHeader(const std::string& senderCompId, const std::string& targetCompId, int msgSeqNum) {
    setField(49, senderCompId);
    setField(56, targetCompId);
    setField(34, std::to_string(msgSeqNum));
    setField(52, formatTimestamp());
}

std::string FIXMessage::serialize() const {
    std::ostringstream oss;
    constexpr char SOH = '\x01';
    
    oss << "8=FIX.4.4" << SOH;
    
    std::ostringstream bodyStream;
    bodyStream << "35=" << static_cast<char>(msgType_) << SOH;
    
    for (const auto& [tag, value] : fields_) {
        if (tag != 8 && tag != 9 && tag != 10) {
            bodyStream << tag << "=" << value << SOH;
        }
    }
    
    std::string body = bodyStream.str();
    oss << "9=" << body.length() << SOH;
    oss << body;
    
    std::string messageWithoutChecksum = oss.str();
    std::string checksum = calculateChecksum();
    oss << "10=" << checksum << SOH;
    
    return oss.str();
}

bool FIXMessage::parse(const std::string& rawMessage) {
    if (rawMessage.empty()) {
        return false;
    }
    
    constexpr char SOH = '\x01';
    fields_.clear();
    
    size_t pos = 0;
    while (pos < rawMessage.length()) {
        size_t equalPos = rawMessage.find('=', pos);
        if (equalPos == std::string::npos) {
            break;
        }
        
        size_t sohPos = rawMessage.find(SOH, equalPos);
        if (sohPos == std::string::npos) {
            break;
        }
        
        int tag = std::stoi(rawMessage.substr(pos, equalPos - pos));
        std::string value = rawMessage.substr(equalPos + 1, sohPos - equalPos - 1);
        
        if (tag == 35 && !value.empty()) {
            msgType_ = static_cast<MsgType>(value[0]);
        }
        
        fields_[tag] = value;
        pos = sohPos + 1;
    }
    
    return validate();
}

bool FIXMessage::validate() const {
    return hasField(8) && hasField(9) && hasField(35);
}

std::string FIXMessage::calculateChecksum() const {
    std::string messageWithoutChecksum = serialize();
    size_t checksumPos = messageWithoutChecksum.find("10=");
    if (checksumPos != std::string::npos) {
        messageWithoutChecksum = messageWithoutChecksum.substr(0, checksumPos);
    }
    
    int sum = 0;
    for (char c : messageWithoutChecksum) {
        sum += static_cast<unsigned char>(c);
    }
    sum %= 256;
    
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(3) << sum;
    return oss.str();
}

int FIXMessage::getMsgSeqNum() const {
    auto seqNum = getField(34);
    return seqNum ? std::stoi(*seqNum) : 0;
}

std::string FIXMessage::getSenderCompId() const {
    auto sender = getField(49);
    return sender ? *sender : "";
}

std::string FIXMessage::getTargetCompId() const {
    auto target = getField(56);
    return target ? *target : "";
}

std::chrono::system_clock::time_point FIXMessage::getTimestamp() const {
    return timestamp_;
}

void FIXMessage::setTimestamp(std::chrono::system_clock::time_point ts) {
    timestamp_ = ts;
}

std::string FIXMessage::formatTimestamp() const {
    auto time = std::chrono::system_clock::to_time_t(timestamp_);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp_.time_since_epoch()) % 1000;
    
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d-%H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count();
    
    return oss.str();
}

bool FIXMessage::validateChecksum(const std::string& rawMessage) const {
    size_t checksumPos = rawMessage.rfind("10=");
    if (checksumPos == std::string::npos) {
        return false;
    }
    
    std::string messageWithoutChecksum = rawMessage.substr(0, checksumPos);
    int sum = 0;
    for (char c : messageWithoutChecksum) {
        sum += static_cast<unsigned char>(c);
    }
    sum %= 256;
    
    constexpr char SOH = '\x01';
    size_t sohPos = rawMessage.find(SOH, checksumPos);
    if (sohPos == std::string::npos) {
        return false;
    }
    
    std::string checksumStr = rawMessage.substr(checksumPos + 3, sohPos - checksumPos - 3);
    int checksum = std::stoi(checksumStr);
    
    return sum == checksum;
}

int FIXMessage::calculateBodyLength() const {
    constexpr char SOH = '\x01';
    std::ostringstream bodyStream;
    
    bodyStream << "35=" << static_cast<char>(msgType_) << SOH;
    
    for (const auto& [tag, value] : fields_) {
        if (tag != 8 && tag != 9 && tag != 10) {
            bodyStream << tag << "=" << value << SOH;
        }
    }
    
    return bodyStream.str().length();
}

}
}
