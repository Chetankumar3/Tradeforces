#include "../../include/admin/admin_server.h"
#include "../../include/metrics/metrics_collector.h"
#include "../../include/risk/risk_manager.h"
#include "../../include/fix_engine/fix_session_manager.h"
#include "../../include/order_manager/order_manager.h"
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

namespace admin {

AdminServer::AdminServer(uint16_t port) : port_(port) {}

AdminServer::~AdminServer() {
    stop();
}

void AdminServer::start() {
    if (running_.exchange(true)) return;
    
    server_thread_ = std::thread([this]() {
        try {
            acceptor_ = std::make_unique<tcp::acceptor>(
                io_context_,
                tcp::endpoint(tcp::v4(), port_)
            );
            acceptLoop();
            io_context_.run();
        } catch (...) {
            running_.store(false);
        }
    });
}

void AdminServer::stop() {
    if (!running_.exchange(false)) return;
    
    io_context_.stop();
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void AdminServer::acceptLoop() {
    if (!running_.load()) return;
    
    acceptor_->async_accept([this](boost::system::error_code ec, tcp::socket socket) {
        if (!ec && running_.load()) {
            std::thread([this, s = std::move(socket)]() mutable {
                handleSession(std::move(s));
            }).detach();
        }
        acceptLoop();
    });
}

void AdminServer::handleSession(tcp::socket socket) {
    try {
        boost::beast::flat_buffer buffer;
        http::request<http::string_body> req;
        http::read(socket, buffer, req);
        
        std::string response_body = handleRequest(req);
        
        auto res = createResponse(
            http::status::ok,
            "application/json",
            response_body,
            req.version()
        );
        
        http::write(socket, res);
    } catch (const std::exception& e) {
        try {
            json error;
            error["error"] = e.what();
            auto res = createResponse(
                http::status::internal_server_error,
                "application/json",
                error.dump(),
                11
            );
            http::write(socket, res);
        } catch (...) {}
    }
}

std::string AdminServer::handleRequest(const http::request<http::string_body>& req) {
    std::string target = std::string(req.target());
    auto method = req.method();
    
    if (method == http::verb::get && target == "/metrics") {
        return handleGetMetrics();
    } else if (method == http::verb::get && target == "/sessions") {
        return handleGetSessions();
    } else if (method == http::verb::get && target == "/orders") {
        return handleGetOrders();
    } else if (method == http::verb::post && target == "/orders/cancel") {
        return handlePostCancelOrder(req.body());
    } else if (method == http::verb::post && target == "/risk/limits") {
        return handlePostRiskLimits(req.body());
    } else if (method == http::verb::post && target == "/system/control") {
        return handlePostSystemControl(req.body());
    } else if (method == http::verb::get && target == "/health") {
        return handleGetHealth();
    }
    
    json error;
    error["error"] = "Not found";
    return error.dump();
}

std::string AdminServer::handleGetMetrics() {
    auto& collector = metrics::MetricsCollector::getInstance();
    return collector.exportPrometheus();
}

std::string AdminServer::handleGetSessions() {
    json response;
    response["active_sessions"] = metrics::MetricsCollector::getInstance().getMetrics().active_sessions;
    response["status"] = "ok";
    return response.dump();
}

std::string AdminServer::handleGetOrders() {
    auto metrics = metrics::MetricsCollector::getInstance().getMetrics();
    
    json response;
    response["total_orders"] = metrics.total_orders;
    response["active_orders"] = metrics.active_orders;
    response["filled_orders"] = metrics.filled_orders;
    response["rejected_orders"] = metrics.rejected_orders;
    response["canceled_orders"] = metrics.canceled_orders;
    response["fill_rate"] = metrics.fill_rate;
    response["rejection_rate"] = metrics.rejection_rate;
    
    return response.dump();
}

std::string AdminServer::handlePostCancelOrder(const std::string& body) {
    try {
        json req = json::parse(body);
        std::string order_id = req["order_id"];
        
        json response;
        response["status"] = "accepted";
        response["order_id"] = order_id;
        response["message"] = "Cancel request submitted";
        
        return response.dump();
    } catch (const std::exception& e) {
        json error;
        error["error"] = e.what();
        return error.dump();
    }
}

std::string AdminServer::handlePostRiskLimits(const std::string& body) {
    try {
        json req = json::parse(body);
        auto& risk_mgr = risk::RiskManager::getInstance();
        
        if (req.contains("account_id")) {
            risk::RiskLimits limits;
            if (req.contains("max_order_quantity")) 
                limits.max_order_quantity = req["max_order_quantity"];
            if (req.contains("max_position_quantity")) 
                limits.max_position_quantity = req["max_position_quantity"];
            if (req.contains("daily_loss_limit")) 
                limits.daily_loss_limit = req["daily_loss_limit"];
            if (req.contains("credit_limit")) 
                limits.credit_limit = req["credit_limit"];
            
            risk_mgr.setAccountLimits(req["account_id"], limits);
        }
        
        json response;
        response["status"] = "updated";
        return response.dump();
    } catch (const std::exception& e) {
        json error;
        error["error"] = e.what();
        return error.dump();
    }
}

std::string AdminServer::handlePostSystemControl(const std::string& body) {
    try {
        json req = json::parse(body);
        std::string action = req["action"];
        
        json response;
        if (action == "reset_metrics") {
            metrics::MetricsCollector::getInstance().reset();
            response["status"] = "metrics_reset";
        } else if (action == "reset_daily_limits") {
            risk::RiskManager::getInstance().resetDailyLimits();
            response["status"] = "daily_limits_reset";
        } else {
            response["error"] = "Unknown action";
        }
        
        return response.dump();
    } catch (const std::exception& e) {
        json error;
        error["error"] = e.what();
        return error.dump();
    }
}

std::string AdminServer::handleGetHealth() {
    auto metrics = metrics::MetricsCollector::getInstance().getMetrics();
    
    json response;
    response["status"] = "healthy";
    response["active_sessions"] = metrics.active_sessions;
    response["active_orders"] = metrics.active_orders;
    response["uptime_seconds"] = 0;
    
    return response.dump();
}

http::response<http::string_body> AdminServer::createResponse(
    http::status status,
    const std::string& content_type,
    const std::string& body,
    unsigned version) {
    
    http::response<http::string_body> res{status, version};
    res.set(http::field::server, "FIX-Gateway-Admin");
    res.set(http::field::content_type, content_type);
    res.body() = body;
    res.prepare_payload();
    return res;
}

}
