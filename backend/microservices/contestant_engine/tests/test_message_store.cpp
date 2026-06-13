#include <gtest/gtest.h>
#include "persistence/message_store.h"
#include <filesystem>

using namespace persistence;
namespace fs = std::filesystem;

class MessageStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_path_ = "./test_store";
        fs::create_directories(test_path_);
        store_ = std::make_unique<MessageStore>(test_path_);
    }
    
    void TearDown() override {
        store_.reset();
        fs::remove_all(test_path_);
    }
    
    std::string test_path_;
    std::unique_ptr<MessageStore> store_;
};

TEST_F(MessageStoreTest, StoreAndRetrieveMessage) {
    bool result = store_->storeMessage("SESSION1", 1, "8=FIX.4.4|9=100|35=D|...", true);
    EXPECT_TRUE(result);
    
    auto messages = store_->getMessages("SESSION1", 1, 1);
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages[0].sequence_number, 1);
    EXPECT_TRUE(messages[0].is_inbound);
}

TEST_F(MessageStoreTest, StoreMultipleMessages) {
    store_->storeMessage("SESSION1", 1, "Message1", true);
    store_->storeMessage("SESSION1", 2, "Message2", false);
    store_->storeMessage("SESSION1", 3, "Message3", true);
    
    auto messages = store_->getMessages("SESSION1", 1, 3);
    EXPECT_EQ(messages.size(), 3);
}

TEST_F(MessageStoreTest, GetMessageRange) {
    for (uint32_t i = 1; i <= 10; ++i) {
        store_->storeMessage("SESSION1", i, "Message" + std::to_string(i), true);
    }
    
    auto messages = store_->getMessages("SESSION1", 3, 7);
    EXPECT_EQ(messages.size(), 5);
    EXPECT_EQ(messages[0].sequence_number, 3);
    EXPECT_EQ(messages[4].sequence_number, 7);
}

TEST_F(MessageStoreTest, GetLastSequenceNumber) {
    store_->storeMessage("SESSION1", 1, "Message1", true);
    store_->storeMessage("SESSION1", 2, "Message2", true);
    store_->storeMessage("SESSION1", 3, "Message3", true);
    
    auto lastSeq = store_->getLastSequenceNumber("SESSION1", true);
    EXPECT_EQ(lastSeq, 3);
}

TEST_F(MessageStoreTest, ReplayMessages) {
    store_->storeMessage("SESSION1", 1, "Message1", true);
    store_->storeMessage("SESSION1", 2, "Message2", false);
    
    std::vector<StoredMessage> replayed;
    bool result = store_->replayMessages("SESSION1", [&replayed](const StoredMessage& msg) {
        replayed.push_back(msg);
    });
    
    EXPECT_TRUE(result);
    EXPECT_EQ(replayed.size(), 2);
}

TEST_F(MessageStoreTest, SaveAndLoadSnapshot) {
    std::string snapshot_data = "OrderBook snapshot data here...";
    
    bool saved = store_->saveOrderBookSnapshot(snapshot_data);
    EXPECT_TRUE(saved);
    
    auto loaded = store_->loadOrderBookSnapshot();
    EXPECT_EQ(loaded, snapshot_data);
}

TEST_F(MessageStoreTest, FlushMessages) {
    store_->storeMessage("SESSION1", 1, "Message1", true);
    store_->flush();
    
    auto messages = store_->getMessages("SESSION1", 1, 1);
    EXPECT_EQ(messages.size(), 1);
}

TEST_F(MessageStoreTest, MultipleSessionsIsolated) {
    store_->storeMessage("SESSION1", 1, "Msg1", true);
    store_->storeMessage("SESSION2", 1, "Msg2", true);
    
    auto session1 = store_->getMessages("SESSION1", 1, 1);
    auto session2 = store_->getMessages("SESSION2", 1, 1);
    
    EXPECT_EQ(session1.size(), 1);
    EXPECT_EQ(session2.size(), 1);
    EXPECT_EQ(session1[0].raw_message, "Msg1");
    EXPECT_EQ(session2[0].raw_message, "Msg2");
}

TEST_F(MessageStoreTest, EmptySessionReturnsEmpty) {
    auto messages = store_->getMessages("NONEXISTENT", 1, 10);
    EXPECT_TRUE(messages.empty());
}
