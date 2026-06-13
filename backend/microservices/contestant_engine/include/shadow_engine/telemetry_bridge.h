#pragma once

#include "fix_engine/fix_message.h"
#include <boost/asio.hpp>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fix_gateway {
namespace shadow_engine {

class TelemetryBridge {
public:
    TelemetryBridge();
    ~TelemetryBridge();

    bool start(const std::string& host, unsigned short port);
    void stop();

    void onExecutionReport(const fix_engine::FIXMessage& report);

private:
    void runAcceptLoop();
    void startAccept();
    void handleAccept(
        boost::system::error_code ec,
        const std::shared_ptr<boost::asio::ip::tcp::socket>& socket);
    void writeToClient(boost::asio::ip::tcp::socket& socket, const std::string& message);
    std::string filterExecutionReport(const fix_engine::FIXMessage& report) const;
    std::string calculateChecksum(const std::string& body) const;

    boost::asio::io_context ioContext_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    std::thread ioThread_;
    std::atomic<bool> running_{false};

    std::mutex clientsMutex_;
    std::vector<std::shared_ptr<boost::asio::ip::tcp::socket>> clients_;

    std::mutex bufferMutex_;
    std::deque<std::string> buffer_;
    static constexpr size_t kMaxBufferedReports = 1000;
};

} // namespace shadow_engine
} // namespace fix_gateway
