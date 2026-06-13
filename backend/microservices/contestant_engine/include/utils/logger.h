#pragma once

#include <memory>
#include <string>
#include <iostream>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace fix_gateway {
namespace utils {

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR,
    CRITICAL
};

class SimpleLogger {
public:
    SimpleLogger(const std::string& name, const std::string& filepath) 
        : name_(name), filepath_(filepath) {
        file_.open(filepath, std::ios::app);
    }
    
    void log(LogLevel level, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        ss << " [" << levelToString(level) << "] " << message;
        if (file_.is_open()) {
            file_ << ss.str() << std::endl;
        }
        std::cout << ss.str() << std::endl;
    }
    
    void info(const std::string& msg) { log(LogLevel::INFO, msg); }
    void debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
    void warn(const std::string& msg) { log(LogLevel::WARN, msg); }
    void error(const std::string& msg) { log(LogLevel::ERROR, msg); }
    
private:
    std::string name_;
    std::string filepath_;
    std::ofstream file_;
    std::mutex mutex_;
    
    std::string levelToString(LogLevel level) {
        switch(level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARN: return "WARN";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::CRITICAL: return "CRITICAL";
            default: return "UNKNOWN";
        }
    }
};

class Logger {
public:
    static Logger& getInstance();
    
    void initialize(const std::string& logPath = "logs", 
                   const std::string& logPattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    
    std::shared_ptr<SimpleLogger> getLogger(const std::string& name = "main");
    std::shared_ptr<SimpleLogger> getSessionLogger(const std::string& sessionId);
    
    void setLogLevel(LogLevel level);
    
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() = default;
    
    std::mutex mutex_;
    bool initialized_{false};
    std::string logPath_;
    std::string logPattern_;
    LogLevel logLevel_{LogLevel::INFO};
    std::unordered_map<std::string, std::shared_ptr<SimpleLogger>> loggers_;
};

}
}
