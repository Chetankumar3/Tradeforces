#include "fix_engine/fix_session_manager.h"
#include "contestant_engine/tcp_ingress.h"
#include "shadow_engine/telemetry_bridge.h"
#include "utils/logger.h"
#include "utils/config_loader.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <cstdlib>

std::atomic<bool> g_running{true};

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        g_running = false;
    }
}

int main(int argc, char* argv[]) {
    using namespace fix_gateway;
    
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    try {
        auto& logger = utils::Logger::getInstance();
        logger.initialize("logs");
        auto mainLogger = logger.getLogger("main");
        mainLogger->info("FIX Trading Gateway starting...");
        
        std::string configPath = "config/fix_config.json";
        if (argc > 1) {
            configPath = argv[1];
        }
        
        auto& configLoader = utils::ConfigLoader::getInstance();
        if (!configLoader.loadConfig(configPath)) {
            mainLogger->error("Failed to load configuration");
            return 1;
        }
        
        std::string rulesPath = "config/trading_rules.json";
        if (!configLoader.loadTradingRules(rulesPath)) {
            mainLogger->warn("Failed to load trading rules, using defaults");
        }
        
        auto sessions = configLoader.getSessions();
        if (sessions.empty()) {
            mainLogger->error("No sessions configured");
            return 1;
        }
        
        auto& sessionManager = fix_engine::FIXSessionManager::getInstance();
        sessionManager.initialize(sessions);

        fix_gateway::shadow_engine::TelemetryBridge telemetryBridge;
        const std::string ingressHost = ::getenv("CONTESTANT_INGRESS_HOST")
            ? ::getenv("CONTESTANT_INGRESS_HOST") : "0.0.0.0";
        const unsigned short ingressPort = static_cast<unsigned short>(
            std::stoi(::getenv("CONTESTANT_INGRESS_PORT")
                ? ::getenv("CONTESTANT_INGRESS_PORT") : "9100"));
        const std::string egressHost = ::getenv("CONTESTANT_EGRESS_HOST")
            ? ::getenv("CONTESTANT_EGRESS_HOST") : "0.0.0.0";
        const unsigned short egressPort = static_cast<unsigned short>(
            std::stoi(::getenv("CONTESTANT_EGRESS_PORT")
                ? ::getenv("CONTESTANT_EGRESS_PORT") : "9101"));

        sessionManager.setExecutionReportSink([&telemetryBridge](const fix_engine::FIXMessage& report) {
            telemetryBridge.onExecutionReport(report);
        });

        if (!telemetryBridge.start(egressHost, egressPort)) {
            mainLogger->error("Contestant egress listener could not be started");
            return 1;
        }

        fix_gateway::contestant_engine::TcpIngress tcpIngress(sessionManager);
        if (!tcpIngress.start(ingressHost, ingressPort)) {
            mainLogger->error("Contestant ingress listener could not be started");
            telemetryBridge.stop();
            return 1;
        }

        sessionManager.start();
        
        mainLogger->info("FIX Trading Gateway running. Press Ctrl+C to stop.");
        
        while (g_running && sessionManager.isRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        mainLogger->info("Shutdown signal received");
        tcpIngress.stop();
        telemetryBridge.stop();
        sessionManager.stop();
        
        mainLogger->info("FIX Trading Gateway stopped");
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
