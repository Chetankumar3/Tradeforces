#pragma once

#include "fix_engine/fix_session_manager.h"

#include <atomic>
#include <boost/asio.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace fix_gateway {
namespace contestant_engine {

class TcpIngress {
public:
    explicit TcpIngress(fix_engine::FIXSessionManager& sessionManager);
    ~TcpIngress();

    bool start(const std::string& host, unsigned short port);
    void stop();

private:
    void acceptLoop();
    void readLoop(boost::asio::ip::tcp::socket& socket);
    void routeMessage(const std::string& rawMessage);
    void normalizeIngressMessage(fix_engine::FIXMessage& message) const;
    bool validateIngressMessage(const std::string& rawMessage) const;

    fix_engine::FIXSessionManager& sessionManager_;
    boost::asio::io_context ioContext_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::mutex socketMutex_;
    std::shared_ptr<boost::asio::ip::tcp::socket> activeSocket_;
};

} // namespace contestant_engine
} // namespace fix_gateway
