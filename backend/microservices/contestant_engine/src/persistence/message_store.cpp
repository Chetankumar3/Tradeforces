#include "../../include/persistence/message_store.h"
#include <filesystem>
#include <sstream>
#include <chrono>

namespace fs = std::filesystem;

namespace persistence {

MessageStore::MessageStore(const std::string& store_path) : store_path_(store_path) {
    fs::create_directories(store_path_);
}

MessageStore::~MessageStore() {
    flush();
}

bool MessageStore::storeMessage(const std::string& session_id, uint32_t seq_num,
                                const std::string& message, bool is_inbound) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        auto filepath = getMessageFilePath(session_id);
        
        if (message_files_.find(session_id) == message_files_.end()) {
            message_files_[session_id] = std::make_unique<std::ofstream>(
                filepath, std::ios::app | std::ios::binary
            );
        }
        
        auto& file = *message_files_[session_id];
        if (!file.is_open()) return false;
        
        auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        
        file << timestamp << "|"
             << seq_num << "|"
             << (is_inbound ? "IN" : "OUT") << "|"
             << message << "\n";
        
        updateSequenceNumber(session_id, seq_num, is_inbound);
        
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<StoredMessage> MessageStore::getMessages(const std::string& session_id,
                                                     uint32_t from_seq, uint32_t to_seq) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<StoredMessage> messages;
    
    try {
        auto filepath = getMessageFilePath(session_id);
        std::ifstream file(filepath);
        if (!file.is_open()) return messages;
        
        std::string line;
        while (std::getline(file, line)) {
            std::istringstream iss(line);
            StoredMessage msg;
            msg.session_id = session_id;
            
            std::string timestamp_str, seq_str, direction;
            std::getline(iss, timestamp_str, '|');
            std::getline(iss, seq_str, '|');
            std::getline(iss, direction, '|');
            std::getline(iss, msg.raw_message);
            
            msg.timestamp = std::stoull(timestamp_str);
            msg.sequence_number = std::stoul(seq_str);
            msg.is_inbound = (direction == "IN");
            
            if (msg.sequence_number >= from_seq && msg.sequence_number <= to_seq) {
                messages.push_back(msg);
            }
        }
    } catch (...) {}
    
    return messages;
}

uint32_t MessageStore::getLastSequenceNumber(const std::string& session_id, bool is_inbound) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        auto filepath = getSequenceFilePath(session_id);
        std::ifstream file(filepath);
        if (!file.is_open()) return 0;
        
        std::string line;
        while (std::getline(file, line)) {
            std::istringstream iss(line);
            std::string direction, seq_str;
            std::getline(iss, direction, '|');
            std::getline(iss, seq_str);
            
            bool is_in = (direction == "IN");
            if (is_in == is_inbound) {
                return std::stoul(seq_str);
            }
        }
    } catch (...) {}
    
    return 0;
}

bool MessageStore::replayMessages(const std::string& session_id,
                                 std::function<void(const StoredMessage&)> callback) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        auto filepath = getMessageFilePath(session_id);
        std::ifstream file(filepath);
        if (!file.is_open()) return false;
        
        std::string line;
        while (std::getline(file, line)) {
            std::istringstream iss(line);
            StoredMessage msg;
            msg.session_id = session_id;
            
            std::string timestamp_str, seq_str, direction;
            std::getline(iss, timestamp_str, '|');
            std::getline(iss, seq_str, '|');
            std::getline(iss, direction, '|');
            std::getline(iss, msg.raw_message);
            
            msg.timestamp = std::stoull(timestamp_str);
            msg.sequence_number = std::stoul(seq_str);
            msg.is_inbound = (direction == "IN");
            
            callback(msg);
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

void MessageStore::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [session_id, file] : message_files_) {
        if (file && file->is_open()) {
            file->flush();
        }
    }
}

bool MessageStore::saveOrderBookSnapshot(const std::string& snapshot_data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        auto filepath = getSnapshotFilePath();
        std::ofstream file(filepath, std::ios::trunc);
        if (!file.is_open()) return false;
        
        auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        file << timestamp << "\n" << snapshot_data;
        
        return true;
    } catch (...) {
        return false;
    }
}

std::string MessageStore::loadOrderBookSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        auto filepath = getSnapshotFilePath();
        std::ifstream file(filepath);
        if (!file.is_open()) return "";
        
        std::string timestamp_line, snapshot_data;
        std::getline(file, timestamp_line);
        
        std::ostringstream oss;
        oss << file.rdbuf();
        snapshot_data = oss.str();
        
        return snapshot_data;
    } catch (...) {
        return "";
    }
}

std::string MessageStore::getMessageFilePath(const std::string& session_id) const {
    return store_path_ + "/" + session_id + "_messages.log";
}

std::string MessageStore::getSequenceFilePath(const std::string& session_id) const {
    return store_path_ + "/" + session_id + "_sequence.dat";
}

std::string MessageStore::getSnapshotFilePath() const {
    return store_path_ + "/orderbook_snapshot.dat";
}

void MessageStore::updateSequenceNumber(const std::string& session_id, uint32_t seq_num, bool is_inbound) {
    auto filepath = getSequenceFilePath(session_id);
    std::ofstream file(filepath, std::ios::trunc);
    if (file.is_open()) {
        file << (is_inbound ? "IN" : "OUT") << "|" << seq_num << "\n";
    }
}

}
