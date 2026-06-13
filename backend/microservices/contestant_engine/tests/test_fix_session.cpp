#include <gtest/gtest.h>
#include "fix_engine/fix_session.h"
#include <boost/asio.hpp>

using namespace fix_gateway::fix_engine;
using namespace fix_gateway::utils;

class FIXSessionTest : public ::testing::Test {
protected:
    void SetUp() override {
        config.sessionId = "TEST_SESSION";
        config.senderCompId = "SENDER";
        config.targetCompId = "TARGET";
        config.heartbeatInterval = 30;
        config.reconnectInterval = 30;
        config.logPath = "logs";
        config.isInitiator = false;
        config.socketAcceptPort = 9876;
        
        session = std::make_unique<FIXSession>(config, ioContext);
    }
    
    void TearDown() override {
        if (session) {
            session->stop();
        }
    }
    
    SessionConfig config;
    boost::asio::io_context ioContext;
    std::unique_ptr<FIXSession> session;
};

TEST_F(FIXSessionTest, InitialState) {
    EXPECT_EQ(session->getState(), SessionState::DISCONNECTED);
    EXPECT_FALSE(session->isLoggedOn());
}

TEST_F(FIXSessionTest, GetSessionId) {
    EXPECT_EQ(session->getSessionId(), "TEST_SESSION");
}

TEST_F(FIXSessionTest, SequenceNumberIncrement) {
    int seqNum1 = session->getNextSeqNum();
    int seqNum2 = session->getNextSeqNum();
    
    EXPECT_EQ(seqNum1, 1);
    EXPECT_EQ(seqNum2, 2);
}

TEST_F(FIXSessionTest, ExpectedSequenceNumber) {
    EXPECT_EQ(session->getExpectedSeqNum(), 1);
}

TEST_F(FIXSessionTest, MessageCallbackRegistration) {
    bool callbackCalled = false;
    
    session->setMessageCallback([&callbackCalled](const FIXMessage& msg) {
        callbackCalled = true;
    });
    
    EXPECT_FALSE(callbackCalled);
}

TEST_F(FIXSessionTest, StateTransitions) {
    EXPECT_EQ(session->getState(), SessionState::DISCONNECTED);
}
