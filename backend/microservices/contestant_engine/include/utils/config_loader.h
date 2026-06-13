#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <optional>

namespace fix_gateway {
namespace utils {

struct SessionConfig {
    std::string sessionId;
    std::string senderCompId;
    std::string targetCompId;
    int heartbeatInterval{30};
    int reconnectInterval{30};
    std::string logPath{"logs"};
    int socketAcceptPort{0};
    std::string socketConnectHost;
    int socketConnectPort{0};
    std::string fixVersion{"FIX.4.4"};
    bool isInitiator{false};
    bool resetSeqNumFlag{false};
    std::string dataDirectory{"store"};
};

struct TradingRules {
    double maxOrderSize{1000000.0};
    double minOrderSize{1.0};
    double maxPositionSize{10000000.0};
    int maxOrdersPerSecond{100};
    std::vector<std::string> tradableSymbols;
};

class ConfigLoader {
public:
    static ConfigLoader& getInstance();
    
    bool loadConfig(const std::string& configPath);
    bool loadTradingRules(const std::string& rulesPath);
    
    std::vector<SessionConfig> getSessions() const;
    std::optional<SessionConfig> getSession(const std::string& sessionId) const;
    TradingRules getTradingRules() const;

    ConfigLoader(const ConfigLoader&) = delete;
    ConfigLoader& operator=(const ConfigLoader&) = delete;

private:
    ConfigLoader() = default;
    
    std::vector<SessionConfig> sessions_;
    TradingRules tradingRules_;
    nlohmann::json config_;
    nlohmann::json rules_;
};

}
}
