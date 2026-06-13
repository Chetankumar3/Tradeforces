#pragma once

#include "../fix_engine/fix_message.h"
#include <functional>
#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <memory>

namespace persistence {

struct StoredMessage {
    std::string session_id;
    uint32_t sequence_number;
    std::string message_type;
    std::string raw_message;
    uint64_t timestamp;
    bool is_inbound;
};

class MessageStore {
public:
    MessageStore(const std::string& store_path);
    ~MessageStore();
    
    bool storeMessage(const std::string& session_id, uint32_t seq_num,
                     const std::string& message, bool is_inbound);
    
    std::vector<StoredMessage> getMessages(const std::string& session_id,
                                          uint32_t from_seq, uint32_t to_seq) const;
    
    uint32_t getLastSequenceNumber(const std::string& session_id, bool is_inbound) const;
    
    bool replayMessages(const std::string& session_id,
                       std::function<void(const StoredMessage&)> callback) const;
    
    void flush();
    
    bool saveOrderBookSnapshot(const std::string& snapshot_data);
    std::string loadOrderBookSnapshot() const;
    
private:
    std::string getMessageFilePath(const std::string& session_id) const;
    std::string getSequenceFilePath(const std::string& session_id) const;
    std::string getSnapshotFilePath() const;
    
    void updateSequenceNumber(const std::string& session_id, uint32_t seq_num, bool is_inbound);
    
    std::string store_path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<std::ofstream>> message_files_;
};

}
