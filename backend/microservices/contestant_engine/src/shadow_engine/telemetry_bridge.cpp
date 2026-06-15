#include "shadow_engine/telemetry_bridge.h"

#include "utils/logger.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace fix_gateway {
namespace shadow_engine {

namespace {
constexpr char kSoh = '\x01';
}

TelemetryBridge::TelemetryBridge() = default;

TelemetryBridge::~TelemetryBridge() {
    stop();
}

bool TelemetryBridge::start(const std::string& host, unsigned short port) {
    if (running_) {
        return true;
    }

    try {
        acceptor_ = std::make_unique<boost::asio::ip::tcp::acceptor>(
            ioContext_,
            boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address(host), port));

        running_ = true;
        startAccept();
        ioThread_ = std::thread([this]() { ioContext_.run(); });

        utils::Logger::getInstance().getLogger("main")->info(
            "Telemetry bridge listening on " + host + ":" + std::to_string(port));
        return true;
    } catch (const std::exception& ex) {
        utils::Logger::getInstance().getLogger("main")->error(
            std::string("Telemetry bridge startup failed: ") + ex.what());
        return false;
    }
}

void TelemetryBridge::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    if (acceptor_) {
        boost::system::error_code ec;
        acceptor_->close(ec);
    }

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.clear();
    }

    ioContext_.stop();
    if (ioThread_.joinable()) {
        ioThread_.join();
    }
}

void TelemetryBridge::onExecutionReport(const fix_engine::FIXMessage& report) {
    if (!running_) {
        return;
    }

    const std::string message = filterExecutionReport(report);
    bool delivered = false;

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (auto& client : clients_) {
            if (client && client->is_open()) {
                try {
                    writeToClient(*client, message);
                    delivered = true;
                } catch (const std::exception&) {
                    client->close();
                }
            }
        }
    }

    if (!delivered) {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        buffer_.push_back(message);
        while (buffer_.size() > kMaxBufferedReports) {
            buffer_.pop_front();
        }
    }
}

void TelemetryBridge::runAcceptLoop() {
    if (!acceptor_ || !running_) {
        return;
    }

    startAccept();
}

void TelemetryBridge::startAccept() {
    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(ioContext_);
    acceptor_->async_accept(*socket, [this, socket](const boost::system::error_code& ec) {
        handleAccept(ec, socket);
    });
}

void TelemetryBridge::handleAccept(
    boost::system::error_code ec,
    const std::shared_ptr<boost::asio::ip::tcp::socket>& socket) {
    if (!running_) {
        return;
    }

    if (!ec) {
        auto client = socket;
        client->set_option(boost::asio::ip::tcp::no_delay(true));
        bool connected = false;

        std::lock_guard<std::mutex> clientsLock(clientsMutex_);
        std::lock_guard<std::mutex> bufferLock(bufferMutex_);
        try {
            for (const auto& message : buffer_) {
                writeToClient(*client, message);
            }
            buffer_.clear();
            clients_.emplace_back(std::move(client));
            connected = true;
        } catch (const std::exception& ex) {
            if (client) {
                client->close();
            }
            utils::Logger::getInstance().getLogger("main")->warn(
                std::string("Failed to flush buffered reports: ") + ex.what());
        }

        if (connected) {
            utils::Logger::getInstance().getLogger("main")->info("Telemetry client connected");
        }
    } else {
        utils::Logger::getInstance().getLogger("main")->warn("Telemetry accept failed: " + ec.message());
    }

    if (running_) {
        startAccept();
    }
}

void TelemetryBridge::writeToClient(boost::asio::ip::tcp::socket& socket, const std::string& message) {
    boost::asio::write(socket, boost::asio::buffer(message));
}

std::string TelemetryBridge::filterExecutionReport(const fix_engine::FIXMessage& report) const {
    constexpr char soh = '\x01';

    std::vector<std::pair<int, std::string>> fields;
    auto push = [&](int tag) {
        if (auto value = report.getField(tag)) {
            fields.emplace_back(tag, *value);
        }
    };

    push(8);
    push(9);
    push(10);
    push(11);
    push(17);
    push(31);
    push(32);
    push(37);
    push(39);
    push(54);
    push(55);
    push(150);
    push(151);
    push(880);
    push(1057);

    std::ostringstream oss;
    oss << "8=FIX.4.2" << soh;

    std::ostringstream body;
    body << "35=8" << soh;
    for (const auto& [tag, value] : fields) {
        if (tag == 8) {
            continue;
        }
        if (tag == 9 || tag == 10) {
            continue;
        }

        body << tag << '=' << value << soh;
    }

    const std::string bodyText = body.str();
    // The telemetry harness uses tag 9 as a TCP framing length. It reads this
    // many bytes after the header, including the fixed-width checksum field.
    const std::string bodyLength = std::to_string(bodyText.size() + 7);
    oss << "9=" << bodyLength << soh;
    oss << bodyText;

    const std::string messageWithoutChecksum = oss.str();
    int sum = 0;
    for (unsigned char ch : messageWithoutChecksum) {
        sum += ch;
    }
    sum %= 256;

    std::ostringstream checksum;
    checksum << std::setw(3) << std::setfill('0') << sum;
    oss << "10=" << checksum.str() << soh;
    return oss.str();
}

std::string TelemetryBridge::calculateChecksum(const std::string& body) const {
    int sum = 0;
    for (unsigned char ch : body) {
        sum += ch;
    }
    sum %= 256;

    std::ostringstream oss;
    oss << std::setw(3) << std::setfill('0') << sum;
    return oss.str();
}

} // namespace shadow_engine
} // namespace fix_gateway
