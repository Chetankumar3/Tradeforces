#include "utils/logger.h"
#include <filesystem>
#include <iostream>

namespace fix_gateway {
namespace utils {

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::initialize(const std::string& logPath, const std::string& logPattern) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return;
    }
    
    logPath_ = logPath;
    logPattern_ = logPattern;
    
    try {
        std::filesystem::create_directories(logPath);
        
        auto logger = std::make_shared<SimpleLogger>("main", logPath + "/fix_gateway.log");
        loggers_["main"] = logger;
        
        initialized_ = true;
        logger->info("Logger initialized successfully");
    } catch (const std::exception& ex) {
        std::cerr << "Logger initialization failed: " << ex.what() << std::endl;
        throw;
    }
}

std::shared_ptr<SimpleLogger> Logger::getLogger(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = loggers_.find(name);
    if (it != loggers_.end()) {
        return it->second;
    }
    
    std::filesystem::create_directories(logPath_);
    auto logger = std::make_shared<SimpleLogger>(name, logPath_ + "/" + name + ".log");
    loggers_[name] = logger;
    
    return logger;
}

std::shared_ptr<SimpleLogger> Logger::getSessionLogger(const std::string& sessionId) {
    return getLogger("session_" + sessionId);
}

void Logger::setLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    logLevel_ = level;
}

}
}
