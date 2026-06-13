#include "fix_engine/fix_session.h"
#include "fix_engine/message_parser.h"
#include "order_manager/order_manager.h"

using namespace boost::asio;
using boost::asio::ip::tcp;

namespace fix_gateway {
namespace fix_engine {

FIXSession::FIXSession(const utils::SessionConfig& config, boost::asio::io_context& ioContext)
    : config_(config)
    , ioContext_(ioContext)
    , socket_(std::make_unique<tcp::socket>(ioContext))
    , heartbeatTimer_(std::make_unique<steady_timer>(ioContext))
    , logger_(utils::Logger::getInstance().getSessionLogger(config.sessionId)) {
    
    if (!config_.isInitiator) {
        acceptor_ = std::make_unique<tcp::acceptor>(ioContext, 
            tcp::endpoint(tcp::v4(), config_.socketAcceptPort));
    }
    
    lastSentTime_ = std::chrono::steady_clock::now();
    lastReceivedTime_ = std::chrono::steady_clock::now();
}

FIXSession::~FIXSession() {
    stop();
}

void FIXSession::start() {
    logger_->info("Starting FIX session: " + config_.sessionId);
    
    if (config_.isInitiator) {
        state_ = SessionState::CONNECTING;
        doConnect();
    } else {
        state_ = SessionState::CONNECTING;
        doAccept();
    }
}

void FIXSession::stop() {
    if (state_ == SessionState::DISCONNECTED) {
        return;
    }
    
    logger_->info("Stopping FIX session: " + config_.sessionId);
    
    if (isLoggedOn()) {
        sendLogout("Session terminated");
    }
    
    state_ = SessionState::DISCONNECTED;
    
    boost::system::error_code ec;
    if (socket_->is_open()) {
        socket_->close(ec);
    }
    
    if (heartbeatTimer_) {
        heartbeatTimer_->cancel();
    }
}

void FIXSession::sendMessage(const FIXMessage& message) {
    if (!socket_->is_open()) {
        logger_->warn("Cannot send message, socket not open");
        return;
    }
    
    FIXMessage msg = message;
    msg.setHeader(config_.senderCompId, config_.targetCompId, getNextSeqNum());
    
    std::string serialized = msg.serialize();
    logger_->debug("Sending: " + serialized);
    
    doWrite(serialized);
    updateLastActivity();
}

void FIXSession::sendLogon() {
    logger_->info("Sending Logon");
    
    FIXMessage logon(MsgType::LOGON);
    logon.setField(98, "0");
    logon.setField(108, std::to_string(config_.heartbeatInterval));
    
    if (config_.resetSeqNumFlag) {
        logon.setField(141, "Y");
    }
    
    state_ = SessionState::LOGGING_ON;
    sendMessage(logon);
}

void FIXSession::sendLogout(const std::string& reason) {
    logger_->info("Sending Logout: " + reason);
    
    FIXMessage logout(MsgType::LOGOUT);
    if (!reason.empty()) {
        logout.setField(58, reason);
    }
    
    state_ = SessionState::LOGGING_OUT;
    sendMessage(logout);
}

void FIXSession::sendHeartbeat(const std::string& testReqId) {
    logger_->debug("Sending Heartbeat");
    
    FIXMessage heartbeat(MsgType::HEARTBEAT);
    if (!testReqId.empty()) {
        heartbeat.setField(112, testReqId);
    }
    
    sendMessage(heartbeat);
}

void FIXSession::sendTestRequest() {
    logger_->debug("Sending TestRequest");
    
    FIXMessage testRequest(MsgType::TEST_REQUEST);
    testRequest.setField(112, std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    
    sendMessage(testRequest);
}
void FIXSession::setMessageCallback(MessageCallback callback) {
    messageCallback_ = callback;
}

void FIXSession::setOrderManager(std::shared_ptr<order_manager::OrderManager> orderManager) {
    orderManager_ = orderManager;
}

SessionState FIXSession::getState() const {
    return state_;
}

std::string FIXSession::getSessionId() const {
    return config_.sessionId;
}

int FIXSession::getNextSeqNum() {
    return outgoingSeqNum_++;
}

int FIXSession::getExpectedSeqNum() const {
    return incomingSeqNum_;
}

bool FIXSession::isLoggedOn() const {
    return state_ == SessionState::LOGGED_ON;
}

void FIXSession::doConnect() {
    logger_->info("Connecting to " + config_.socketConnectHost + ":" + std::to_string(config_.socketConnectPort));
    
    tcp::resolver resolver(ioContext_);
    auto endpoints = resolver.resolve(config_.socketConnectHost, 
                                      std::to_string(config_.socketConnectPort));
    
    async_connect(*socket_, endpoints,
        [this](const boost::system::error_code& ec, const tcp::endpoint&) {
            handleConnect(ec);
        });
}

void FIXSession::doAccept() {
    logger_->info("Accepting connections on port " + std::to_string(config_.socketAcceptPort));
    
    acceptor_->async_accept(*socket_,
        [this](const boost::system::error_code& ec) {
            handleAccept(ec);
        });
}

void FIXSession::doRead() {
    socket_->async_read_some(buffer(readBuffer_),
        [this](const boost::system::error_code& ec, std::size_t bytes) {
            handleRead(ec, bytes);
        });
}

void FIXSession::doWrite(const std::string& data) {
    async_write(*socket_, buffer(data),
        [this](const boost::system::error_code& ec, std::size_t bytes) {
            handleWrite(ec, bytes);
        });
}

void FIXSession::handleConnect(const boost::system::error_code& ec) {
    if (ec) {
        logger_->error("Connection failed: " + ec.message());
        state_ = SessionState::RECONNECTING;
        reconnect();
        return;
    }
    
    logger_->info("Connected successfully");
    state_ = SessionState::CONNECTED;
    
    doRead();
    sendLogon();
    startHeartbeatTimer();
}

void FIXSession::handleAccept(const boost::system::error_code& ec) {
    if (ec) {
        logger_->error("Accept failed: " + ec.message());
        return;
    }
    
    logger_->info("Accepted connection");
    state_ = SessionState::CONNECTED;
    
    doRead();
    startHeartbeatTimer();
}

void FIXSession::handleRead(const boost::system::error_code& ec, std::size_t bytesTransferred) {
    if (ec) {
        logger_->error("Read error: " + ec.message());
        stop();
        reconnect();
        return;
    }
    
    receiveBuffer_.append(readBuffer_.data(), bytesTransferred);
    updateLastActivity();
    
    constexpr char SOH = '\x01';
    size_t pos = 0;
    
    while ((pos = receiveBuffer_.find("10=", pos)) != std::string::npos) {
        size_t endPos = receiveBuffer_.find(SOH, pos + 3);
        if (endPos == std::string::npos) {
            break;
        }
        
        std::string rawMessage = receiveBuffer_.substr(0, endPos + 1);
        receiveBuffer_.erase(0, endPos + 1);
        
        logger_->debug("Received: " + rawMessage);
        processMessage(rawMessage);
        
        pos = 0;
    }
    
    doRead();
}

void FIXSession::handleWrite(const boost::system::error_code& ec, std::size_t bytesTransferred) {
    if (ec) {
        logger_->error("Write error: " + ec.message());
        stop();
        return;
    }
    
    logger_->debug("Wrote " + std::to_string(bytesTransferred) + " bytes");
}

void FIXSession::processMessage(const std::string& rawMessage) {
    metrics::MetricsCollector::getInstance().recordMessageProcessed();
    
    auto message = MessageParser::parse(rawMessage);
    if (!message) {
        logger_->warn("Failed to parse message");
        return;
    }
    
    if (!validateSeqNum(*message)) {
        logger_->error("Sequence number validation failed");
        return;
    }
    
    incomingSeqNum_++;
    
    switch (message->getMsgType()) {
        case MsgType::LOGON:
            handleLogon(*message);
            break;
        case MsgType::LOGOUT:
            handleLogout(*message);
            break;
        case MsgType::HEARTBEAT:
            handleHeartbeat(*message);
            break;
        case MsgType::TEST_REQUEST:
            handleTestRequest(*message);
            break;
        case MsgType::RESEND_REQUEST:
            handleResendRequest(*message);
            break;
        case MsgType::SEQUENCE_RESET:
            handleSequenceReset(*message);
            break;
        case MsgType::NEW_ORDER_SINGLE:
            if (orderManager_) {
                orderManager_->handleNewOrderSingle(*message);
            } else if (messageCallback_) {
                messageCallback_(*message);
            }
            break;
        case MsgType::ORDER_CANCEL_REQUEST:
            if (orderManager_) {
                orderManager_->handleOrderCancelRequest(*message);
            } else if (messageCallback_) {
                messageCallback_(*message);
            }
            break;
        case MsgType::ORDER_CANCEL_REPLACE_REQUEST:
            if (orderManager_) {
                orderManager_->handleOrderCancelReplaceRequest(*message);
            } else if (messageCallback_) {
                messageCallback_(*message);
            }
            break;
        case MsgType::MARKET_DATA_REQUEST:
            handleMarketDataRequest(*message);
            break;
        default:
            if (messageCallback_) {
                messageCallback_(*message);
            }
            break;
    }
}

void FIXSession::handleLogon(const FIXMessage& message) {
    logger_->info("Received Logon");
    
    if (state_ == SessionState::CONNECTED) {
        sendLogon();
    }
    
    state_ = SessionState::LOGGED_ON;
    logger_->info("Session logged on");
}

void FIXSession::handleLogout(const FIXMessage& message) {
    auto reason = message.getField(58);
    logger_->info("Received Logout: " + reason.value_or("No reason"));
    
    if (state_ != SessionState::LOGGING_OUT) {
        sendLogout("Acknowledged");
    }
    
    stop();
}

void FIXSession::handleHeartbeat(const FIXMessage& message) {
    logger_->debug("Received Heartbeat");
}

void FIXSession::handleTestRequest(const FIXMessage& message) {
    auto testReqId = message.getField(112);
    logger_->debug("Received TestRequest: " + testReqId.value_or(""));
    
    sendHeartbeat(testReqId.value_or(""));
}

void FIXSession::handleResendRequest(const FIXMessage& message) {
    auto beginSeqNo = message.getField(7);
    auto endSeqNo = message.getField(16);
    
    logger_->warn("Received ResendRequest: " + beginSeqNo.value_or("0") + " to " + endSeqNo.value_or("0"));
}

void FIXSession::handleSequenceReset(const FIXMessage& message) {
    auto newSeqNo = message.getField(36);
    logger_->info("Received SequenceReset: " + newSeqNo.value_or("0"));
    
    if (newSeqNo) {
        incomingSeqNum_ = std::stoi(*newSeqNo);
    }
}

void FIXSession::startHeartbeatTimer() {
    heartbeatTimer_->expires_after(std::chrono::seconds(config_.heartbeatInterval));
    heartbeatTimer_->async_wait(
        [this](const boost::system::error_code& ec) {
            handleHeartbeatTimeout(ec);
        });
}

void FIXSession::handleHeartbeatTimeout(const boost::system::error_code& ec) {
    if (ec) {
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastSent = std::chrono::duration_cast<std::chrono::seconds>(now - lastSentTime_).count();
    auto timeSinceLastReceived = std::chrono::duration_cast<std::chrono::seconds>(now - lastReceivedTime_).count();
    
    if (timeSinceLastSent >= config_.heartbeatInterval) {
        sendHeartbeat();
    }
    
    if (timeSinceLastReceived >= (config_.heartbeatInterval * 2)) {
        logger_->warn("No heartbeat received, sending TestRequest");
        sendTestRequest();
    }
    
    startHeartbeatTimer();
}

void FIXSession::reconnect() {
    if (!config_.isInitiator) {
        return;
    }
    
    logger_->info("Reconnecting in " + std::to_string(config_.reconnectInterval) + " seconds");
    
    auto timer = std::make_shared<steady_timer>(ioContext_);
    timer->expires_after(std::chrono::seconds(config_.reconnectInterval));
    timer->async_wait([this, timer](const boost::system::error_code& ec) {
        if (!ec) {
            reset();
            start();
        }
    });
}

void FIXSession::reset() {
    receiveBuffer_.clear();
    
    if (config_.resetSeqNumFlag) {
        outgoingSeqNum_ = 1;
        incomingSeqNum_ = 1;
    }
    
    socket_ = std::make_unique<tcp::socket>(ioContext_);
}

bool FIXSession::validateSeqNum(const FIXMessage& message) {
    int receivedSeqNum = message.getMsgSeqNum();
    int expectedSeqNum = incomingSeqNum_;
    
    if (receivedSeqNum == expectedSeqNum) {
        return true;
    }
    
    if (receivedSeqNum > expectedSeqNum) {
        logger_->warn("Gap detected: expected " + std::to_string(expectedSeqNum) + ", received " + std::to_string(receivedSeqNum));
        return true;
    }
    
    logger_->error("Sequence too low: expected " + std::to_string(expectedSeqNum) + ", received " + std::to_string(receivedSeqNum));
    return false;
}

void FIXSession::updateLastActivity() {
    auto now = std::chrono::steady_clock::now();
    lastSentTime_ = now;
    lastReceivedTime_ = now;
}

void FIXSession::handleMarketDataRequest(const FIXMessage& message) {
    logger_->info("Received Market Data Request");
    
    if (marketDataCallback_) {
        marketDataCallback_(config_.sessionId, message);
    }
}

void FIXSession::setMarketDataCallback(MarketDataCallback callback) {
    marketDataCallback_ = std::move(callback);
}

}
}
