#pragma once

#include "fix_message.h"
#include "utils/config_loader.h"
#include "utils/logger.h"
#include <string>
#include <memory>
#include <atomic>
#include <chrono>
#include <functional>
#include <boost/asio.hpp>

namespace fix_gateway {

namespace order_manager {
    class OrderManager;
}

namespace fix_engine {

enum class SessionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    LOGGING_ON,
    LOGGED_ON,
    LOGGING_OUT,
    RECONNECTING
};

class FIXSession {
public:
    using MessageCallback = std::function<void(const FIXMessage&)>;
    using MarketDataCallback = std::function<void(const std::string&, const FIXMessage&)>;
    
    explicit FIXSession(const utils::SessionConfig& config, boost::asio::io_context& ioContext);
    ~FIXSession();
    
    void start();
    void stop();
    
    void sendMessage(const FIXMessage& message);
    void setMessageCallback(MessageCallback callback);
    void setMarketDataCallback(MarketDataCallback callback);
    void setOrderManager(std::shared_ptr<order_manager::OrderManager> orderManager);
    
    void sendLogon();
    void sendLogout(const std::string& reason = "");
    void sendHeartbeat(const std::string& testReqId = "");
    void sendTestRequest();
    
    SessionState getState() const;
    std::string getSessionId() const;
    int getNextSeqNum();
    int getExpectedSeqNum() const;
    
    bool isLoggedOn() const;

private:
    utils::SessionConfig config_;
    boost::asio::io_context& ioContext_;
    std::unique_ptr<boost::asio::ip::tcp::socket> socket_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    std::unique_ptr<boost::asio::steady_timer> heartbeatTimer_;
    SessionState state_{SessionState::DISCONNECTED};
    std::atomic<int> outgoingSeqNum_{1};
    std::atomic<int> incomingSeqNum_{1};
    MessageCallback messageCallback_;
    MarketDataCallback marketDataCallback_;
    std::shared_ptr<order_manager::OrderManager> orderManager_;
    std::shared_ptr<utils::SimpleLogger> logger_;
    
    std::chrono::steady_clock::time_point lastSentTime_;
    std::chrono::steady_clock::time_point lastReceivedTime_;
    
    std::string receiveBuffer_;
    std::array<char, 8192> readBuffer_;
    
    void doConnect();
    void doAccept();
    void doRead();
    void doWrite(const std::string& data);
    
    void handleConnect(const boost::system::error_code& ec);
    void handleAccept(const boost::system::error_code& ec);
    void handleRead(const boost::system::error_code& ec, std::size_t bytesTransferred);
    void handleWrite(const boost::system::error_code& ec, std::size_t bytesTransferred);
    
    void processMessage(const std::string& rawMessage);
    void handleLogon(const FIXMessage& message);
    void handleLogout(const FIXMessage& message);
    void handleHeartbeat(const FIXMessage& message);
    void handleTestRequest(const FIXMessage& message);
    void handleResendRequest(const FIXMessage& message);
    void handleSequenceReset(const FIXMessage& message);
    void handleMarketDataRequest(const FIXMessage& message);
    
    void startHeartbeatTimer();
    void handleHeartbeatTimeout(const boost::system::error_code& ec);
    
    void reconnect();
    void reset();
    
    bool validateSeqNum(const FIXMessage& message);
    void updateLastActivity();
};

}
}
