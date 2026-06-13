#include "utils/config_loader.h"
#include "utils/logger.h"
#include <fstream>
#include <stdexcept>

namespace fix_gateway {
namespace utils {

ConfigLoader& ConfigLoader::getInstance() {
    static ConfigLoader instance;
    return instance;
}

bool ConfigLoader::loadConfig(const std::string& configPath) {
    try {
        std::ifstream file(configPath);
        if (!file.is_open()) {
            Logger::getInstance().getLogger("main")->error("Failed to open config file: " + configPath);
            return false;
        }
        
        config_ = nlohmann::json::parse(file);
        
        if (!config_.contains("sessions") || !config_["sessions"].is_array()) {
            Logger::getInstance().getLogger("main")->error("Invalid config: missing sessions array");
            return false;
        }
        
        sessions_.clear();
        
        for (const auto& sessionJson : config_["sessions"]) {
            SessionConfig session;
            session.sessionId = sessionJson.value("sessionId", "");
            session.senderCompId = sessionJson.value("senderCompId", "");
            session.targetCompId = sessionJson.value("targetCompId", "");
            session.heartbeatInterval = sessionJson.value("heartbeatInterval", 30);
            session.reconnectInterval = sessionJson.value("reconnectInterval", 30);
            session.logPath = sessionJson.value("logPath", "logs");
            session.socketAcceptPort = sessionJson.value("socketAcceptPort", 0);
            session.socketConnectHost = sessionJson.value("socketConnectHost", "");
            session.socketConnectPort = sessionJson.value("socketConnectPort", 0);
            session.fixVersion = sessionJson.value("fixVersion", "FIX.4.4");
            session.isInitiator = sessionJson.value("isInitiator", false);
            session.resetSeqNumFlag = sessionJson.value("resetSeqNumFlag", false);
            session.dataDirectory = sessionJson.value("dataDirectory", "store");
            
            sessions_.push_back(session);
        }
        
        Logger::getInstance().getLogger("main")->info("Loaded " + std::to_string(sessions_.size()) + " FIX session(s) from config");
        return true;
        
    } catch (const std::exception& e) {
        Logger::getInstance().getLogger("main")->error(std::string("Exception loading config: ") + e.what());
        return false;
    }
}

bool ConfigLoader::loadTradingRules(const std::string& rulesPath) {
    try {
        std::ifstream file(rulesPath);
        if (!file.is_open()) {
            Logger::getInstance().getLogger("main")->error("Failed to open trading rules file: " + rulesPath);
            return false;
        }
        
        rules_ = nlohmann::json::parse(file);
        
        tradingRules_.maxOrderSize = rules_.value("maxOrderSize", 1000000.0);
        tradingRules_.minOrderSize = rules_.value("minOrderSize", 1.0);
        tradingRules_.maxPositionSize = rules_.value("maxPositionSize", 10000000.0);
        tradingRules_.maxOrdersPerSecond = rules_.value("maxOrdersPerSecond", 100);
        
        if (rules_.contains("tradableSymbols") && rules_["tradableSymbols"].is_array()) {
            for (const auto& symbol : rules_["tradableSymbols"]) {
                tradingRules_.tradableSymbols.push_back(symbol.get<std::string>());
            }
        }
        
        Logger::getInstance().getLogger("main")->info("Loaded trading rules with " + std::to_string(tradingRules_.tradableSymbols.size()) + " tradable symbols");
        return true;
        
    } catch (const std::exception& e) {
        Logger::getInstance().getLogger("main")->error(std::string("Exception loading trading rules: ") + e.what());
        return false;
    }
}

std::vector<SessionConfig> ConfigLoader::getSessions() const {
    return sessions_;
}

std::optional<SessionConfig> ConfigLoader::getSession(const std::string& sessionId) const {
    for (const auto& session : sessions_) {
        if (session.sessionId == sessionId) {
            return session;
        }
    }
    return std::nullopt;
}

TradingRules ConfigLoader::getTradingRules() const {
    return tradingRules_;
}

}
}
