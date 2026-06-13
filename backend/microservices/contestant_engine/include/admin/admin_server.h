#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <string>
#include <memory>
#include <thread>
#include <atomic>

namespace http = boost::beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace admin {

class AdminServer {
public:
    AdminServer(uint16_t port);
    ~AdminServer();
    
    void start();
    void stop();
    
    bool isRunning() const { return running_.load(); }
    
private:
    void acceptLoop();
    void handleSession(tcp::socket socket);
    
    std::string handleRequest(const http::request<http::string_body>& req);
    std::string handleGetMetrics();
    std::string handleGetSessions();
    std::string handleGetOrders();
    std::string handlePostCancelOrder(const std::string& body);
    std::string handlePostRiskLimits(const std::string& body);
    std::string handlePostSystemControl(const std::string& body);
    std::string handleGetHealth();
    
    http::response<http::string_body> createResponse(
        http::status status,
        const std::string& content_type,
        const std::string& body,
        unsigned version);
    
    uint16_t port_;
    std::atomic<bool> running_{false};
    net::io_context io_context_;
    std::unique_ptr<tcp::acceptor> acceptor_;
    std::thread server_thread_;
};

}
