#include "contestant_engine/tcp_ingress.h"

#include "utils/logger.h"

#include <chrono>
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>

namespace fix_gateway {
namespace contestant_engine {

namespace {
constexpr char kSoh = '\x01';
constexpr size_t kMaxMessageSize = 64 * 1024;

std::string getenvOr(const char* name, const std::string& fallback) {
    const char* value = ::getenv(name);
    return value ? std::string(value) : fallback;
}

std::string currentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) %
                    1000;
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d-%H:%M:%S") << '.'
        << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

bool hasRequiredFields(const fix_engine::FIXMessage& message) {
    if (!message.hasField(11) || !message.hasField(35)) {
        return false;
    }

    switch (message.getMsgType()) {
        case fix_engine::MsgType::NEW_ORDER_SINGLE:
            return message.hasField(38) && message.hasField(40) &&
                   message.hasField(44) && message.hasField(54) &&
                   message.hasField(55) && message.hasField(59);
        case fix_engine::MsgType::ORDER_CANCEL_REQUEST:
            return message.hasField(41);
        default:
            return false;
    }
}

bool parseUnsigned(const std::string& text, size_t& value) {
    if (text.empty()) {
        return false;
    }

    value = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        const size_t digit = static_cast<size_t>(ch - '0');
        if (value > (kMaxMessageSize - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    return true;
}
} // namespace

TcpIngress::TcpIngress(fix_engine::FIXSessionManager& sessionManager)
    : sessionManager_(sessionManager) {}

TcpIngress::~TcpIngress() {
    stop();
}

bool TcpIngress::start(const std::string& host, unsigned short port) {
    if (running_) {
        return true;
    }

    try {
        acceptor_ = std::make_unique<boost::asio::ip::tcp::acceptor>(
            ioContext_,
            boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address(host), port));
        running_ = true;
        worker_ = std::thread([this]() { acceptLoop(); });

        utils::Logger::getInstance().getLogger("main")->info(
            "Contestant ingress listening on " + host + ":" + std::to_string(port));
        return true;
    } catch (const std::exception& ex) {
        utils::Logger::getInstance().getLogger("main")->error(
            std::string("Contestant ingress startup failed: ") + ex.what());
        return false;
    }
}

void TcpIngress::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (acceptor_) {
        boost::system::error_code ec;
        acceptor_->close(ec);
    }
    {
        std::lock_guard<std::mutex> lock(socketMutex_);
        if (activeSocket_) {
            boost::system::error_code ec;
            activeSocket_->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            activeSocket_->close(ec);
        }
    }
    ioContext_.stop();

    if (worker_.joinable()) {
        worker_.join();
    }
}

void TcpIngress::acceptLoop() {
    auto logger = utils::Logger::getInstance().getLogger("main");

    while (running_) {
        try {
            auto socket = std::make_shared<boost::asio::ip::tcp::socket>(ioContext_);
            acceptor_->accept(*socket);
            socket->set_option(boost::asio::ip::tcp::no_delay(true));
            {
                std::lock_guard<std::mutex> lock(socketMutex_);
                activeSocket_ = socket;
            }
            boost::system::error_code endpointEc;
            const auto remoteEndpoint = socket->remote_endpoint(endpointEc);
            std::string remote = "<unknown>";
            if (!endpointEc) {
                remote = remoteEndpoint.address().to_string() + ":" +
                         std::to_string(remoteEndpoint.port());
            }
            logger->info("Contestant ingress connection established from " + remote);
            readLoop(*socket);
            {
                std::lock_guard<std::mutex> lock(socketMutex_);
                if (activeSocket_ == socket) {
                    activeSocket_.reset();
                }
            }
        } catch (const boost::system::system_error& ex) {
            if (running_) {
                logger->warn(std::string("Contestant ingress connection failed: ") + ex.what());
            }
        }
    }
}

void TcpIngress::readLoop(boost::asio::ip::tcp::socket& socket) {
    std::string receiveBuffer;
    receiveBuffer.reserve(8192);
    char chunk[8192];
    bool loggedFirstRead = false;

    while (running_ && socket.is_open()) {
        boost::system::error_code ec;
        const size_t bytesRead = socket.read_some(boost::asio::buffer(chunk), ec);
        if (ec) {
            if (ec != boost::asio::error::eof && running_) {
                utils::Logger::getInstance().getLogger("main")->warn(
                    "Contestant ingress read failed: " + ec.message());
            }
            return;
        }

        receiveBuffer.append(chunk, bytesRead);
        if (!loggedFirstRead) {
            loggedFirstRead = true;
            utils::Logger::getInstance().getLogger("main")->info(
                "Contestant ingress received first TCP bytes: bytes_read=" +
                std::to_string(bytesRead) + ", buffered=" + std::to_string(receiveBuffer.size()));
        }
        if (receiveBuffer.size() > kMaxMessageSize &&
            receiveBuffer.find(std::string(1, kSoh) + "10=") == std::string::npos) {
            utils::Logger::getInstance().getLogger("main")->warn(
                "Discarding oversized incomplete ingress message");
            receiveBuffer.clear();
            continue;
        }

        while (true) {
            const std::string checksumMarker = std::string(1, kSoh) + "10=";
            const size_t checksumPos = receiveBuffer.find(checksumMarker);
            if (checksumPos == std::string::npos) {
                break;
            }

            const size_t messageEnd = receiveBuffer.find(kSoh, checksumPos + checksumMarker.size());
            if (messageEnd == std::string::npos) {
                break;
            }

            std::string rawMessage = receiveBuffer.substr(0, messageEnd + 1);
            receiveBuffer.erase(0, messageEnd + 1);
            routeMessage(rawMessage);
        }
    }
}

void TcpIngress::routeMessage(const std::string& rawMessage) {
    auto logger = utils::Logger::getInstance().getLogger("main");

    logger->info("[contestant ingress] parse[0] rawLen=" + std::to_string(rawMessage.size()) +
                 ", start=" + std::string(rawMessage.rfind("8=FIX.4.2") != std::string::npos ? "ok" : "missing"));

    if (!validateIngressMessage(rawMessage)) {
        logger->warn("Skipping invalid contestant ingress FIX message (custom parser rejected the framing)");
        return;
    }

    fix_engine::FIXMessage message;
    try {
        if (!message.parse(rawMessage) || !hasRequiredFields(message)) {
            logger->warn("Skipping unsupported or incomplete contestant ingress FIX message");
            return;
        }
    } catch (const std::exception& ex) {
        logger->warn(std::string("Failed to parse contestant ingress FIX message: ") + ex.what());
        return;
    }

    normalizeIngressMessage(message);

    const auto clOrdId = message.getField(11).value_or("<missing>");
    const auto symbol = message.getField(55).value_or("<missing>");
    const auto msgType = message.getMsgType();
    logger->info("[contestant ingress] parse[1] msgType=" + std::to_string(static_cast<int>(msgType)) +
                 ", ClOrdID=" + clOrdId + ", Symbol=" + symbol);

    const std::string sessionId =
        getenvOr("CONTESTANT_ENGINE_SESSION_ID", "CLIENT_SESSION_1");
    std::shared_ptr<order_manager::OrderManager> orderManager;
    orderManager = sessionManager_.getOrderManager(sessionId);
    if (!orderManager) {
        const auto sessionIds = sessionManager_.getSessionIds();
        if (!sessionIds.empty()) {
            orderManager = sessionManager_.getOrderManager(sessionIds.front());
        }
    }

    if (!orderManager) {
        logger->warn("No FIX session available for contestant ingress");
        return;
    }

    logger->info("[contestant ingress] parse[2] session=" + sessionId +
                 ", orderManager=ok, msgType=" + std::to_string(static_cast<int>(message.getMsgType())));

    switch (message.getMsgType()) {
        case fix_engine::MsgType::NEW_ORDER_SINGLE:
            logger->info("[contestant ingress] parse[3] dispatch=NEW_ORDER_SINGLE");
            orderManager->handleNewOrderSingle(message);
            break;
        case fix_engine::MsgType::ORDER_CANCEL_REQUEST:
            logger->info("[contestant ingress] parse[3] dispatch=ORDER_CANCEL_REQUEST");
            orderManager->handleOrderCancelRequest(message);
            break;
        default:
            logger->warn("Ignoring unsupported contestant ingress FIX message type");
            break;
    }
}

void TcpIngress::normalizeIngressMessage(fix_engine::FIXMessage& message) const {
    if (!message.hasField(49)) {
        message.setField(49, getenvOr("FIX_SENDER_COMP_ID", "TELEMETRY"));
    }
    if (!message.hasField(56)) {
        message.setField(56, getenvOr("FIX_TARGET_COMP_ID", "CONTESTANT_ENGINE"));
    }
    if (!message.hasField(34)) {
        message.setField(34, "1");
    }
    if (!message.hasField(52)) {
        message.setField(52, currentTimestamp());
    }
}

bool TcpIngress::validateIngressMessage(const std::string& rawMessage) const {
    std::string trace = "";
    bool isValid = false;

    do {
        trace += "0";
        const std::string begin = "8=FIX.4.2";
        if (rawMessage.compare(0, begin.size(), begin) != 0 ||
            rawMessage.size() < begin.size() + 1 ||
            rawMessage[begin.size()] != kSoh) {
            break;
        }

        trace += " 1";
        const size_t bodyLengthStart = begin.size() + 1;
        if (rawMessage.compare(bodyLengthStart, 2, "9=") != 0) {
            break;
        }

        trace += " 2";
        const size_t secondSoh = rawMessage.find(kSoh, bodyLengthStart);
        if (secondSoh == std::string::npos) {
            break;
        }

        trace += " 3";
        const std::string bodyLengthText =
            rawMessage.substr(bodyLengthStart + 2, secondSoh - bodyLengthStart - 2);
        size_t bodyLength = 0;
        if (!parseUnsigned(bodyLengthText, bodyLength)) {
            break;
        }

        trace += " 4";
        const std::string checksumMarker = std::string(1, kSoh) + "10=";
        const size_t checksumPos = rawMessage.rfind(checksumMarker);
        if (checksumPos == std::string::npos) {
            break;
        }

        trace += " 5";
        const size_t checksumFieldEnd = rawMessage.find(kSoh, checksumPos + checksumMarker.size());
        if (checksumFieldEnd == std::string::npos || checksumFieldEnd != rawMessage.size() - 1) {
            break;
        }

        trace += " 6";
        const size_t bodyLengthFromFrame = checksumPos - secondSoh;
        if (bodyLength != bodyLengthFromFrame) {
            break;
        }

        trace += " 7";
        const std::string checksumText = rawMessage.substr(checksumPos + 4, 3);
        if (checksumText.find_first_not_of("0123456789") != std::string::npos) {
            break;
        }

        trace += " 8";
        unsigned int sum = 0;
        for (size_t i = 0; i <= checksumPos; ++i) {
            sum += static_cast<unsigned char>(rawMessage[i]);
        }

        if (sum % 256 != static_cast<unsigned int>(std::stoi(checksumText))) {
            break;
        }

        trace += " PASS";
        isValid = true;
    } while (false);

    auto logger = utils::Logger::getInstance().getLogger("main");
    logger->info("[validateIngressMessage] trace: " + trace + " | result: " + (isValid ? "true" : "false"));

    return isValid;
}

} // namespace contestant_engine
} // namespace fix_gateway
