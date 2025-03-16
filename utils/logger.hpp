#pragma once
#include "helper.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>

enum class LogLevel { INFO, WARNING, ERROR, DEBUG };

// Domain-specific log levels for PLAIN domain
enum class PlainLogLevel { WS_REQUEST, WS_RESPONSE, WS_BROADCAST, CURL_REQUEST, CURL_RESPONSE };

// Logger class to manage log entries and output to a specified file
class Logger {
public:
    explicit Logger(const std::string& filePath)
        : logFile_(filePath, std::ios::app) {
        if(!logFile_.is_open()) {
            throw std::runtime_error("Failed to open log file.");
        }
    }

    template<typename... Args>
    void log(const std::string& domain, LogLevel level, const Args&... args) {
        uint64_t currTimestamp = helper::get_current_timestamp_ns();
        std::lock_guard<std::mutex> lock(logMutex_);

        time_t seconds = currTimestamp / 1000000000;
        int nanoseconds = currTimestamp % 1000000000;
        struct tm* timeinfo = localtime(&seconds);

        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

        std::stringstream logStream;
        logStream << buffer << "." << std::setfill('0') << std::setw(6) << (nanoseconds / 1000) << " | ";
        logStream << domain << " | ";
        logStream << logLevelToString(level) << " | ";
        (appendToStream(logStream, args), ...);

        logFile_ << logStream.str() << std::endl;
        logFile_.flush();
    }

    template<typename... Args>
    void log(const std::string& domain, PlainLogLevel level, const Args&... args) {
        uint64_t currTimestamp = helper::get_current_timestamp_ns();
        std::lock_guard<std::mutex> lock(logMutex_);

        time_t seconds = currTimestamp / 1000000000;
        int nanoseconds = currTimestamp % 1000000000;
        struct tm* timeinfo = localtime(&seconds);

        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

        std::stringstream logStream;
        logStream << buffer << "." << std::setfill('0') << std::setw(6) << (nanoseconds / 1000) << " | ";
        logStream << domain << " | ";
        logStream << plainLogLevelToString(level) << " | ";
        (appendToStream(logStream, args), ...);

        logFile_ << logStream.str() << std::endl;
        logFile_.flush();
    }

private:
    std::ofstream logFile_;
    std::mutex logMutex_; // Ensure thread-safe logging

    template<typename T>
    static void appendToStream(std::stringstream& ss, const T& value) {
        if constexpr(std::is_same_v<T, double>) {
            ss << std::fixed << std::setprecision(6) << value;
        } else if constexpr(std::is_same_v<T, bool>) {
            ss << std::boolalpha << value;
        } else {
            ss << value;
        }
    }

    static std::string logLevelToString(LogLevel level) {
        switch(level) {
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARN";
        case LogLevel::ERROR: return "ERRO";
        case LogLevel::DEBUG: return "DEBG";
        default: return "UNKNOWN";
        }
    }

    static std::string plainLogLevelToString(PlainLogLevel level) {
        switch(level) {
        case PlainLogLevel::WS_REQUEST: return "WREQ";
        case PlainLogLevel::WS_RESPONSE: return "WRSP";
        case PlainLogLevel::WS_BROADCAST: return "WBCT";
        case PlainLogLevel::CURL_REQUEST: return "CREQ";
        case PlainLogLevel::CURL_RESPONSE: return "CRSP";
        default: return "UNKNOWN";
        }
    }

public:
    // Domain-specific loggers
    class DomainLogger {
    public:
        DomainLogger(Logger& logger, const std::string& domain)
            : logger_(logger)
            , domain_(domain) {}

        template<typename... Args>
        void debug(const Args&... args) {
#ifdef DEBUG_MODE
            logger_.log(domain_, LogLevel::DEBUG, args...);
#endif
        }

        template<typename... Args>
        void info(const Args&... args) {
            logger_.log(domain_, LogLevel::INFO, args...);
        }

        template<typename... Args>
        void warning(const Args&... args) {
            logger_.log(domain_, LogLevel::WARNING, args...);
        }

        template<typename... Args>
        void error(const Args&... args) {
            logger_.log(domain_, LogLevel::ERROR, args...);
        }

    private:
        Logger& logger_;
        std::string domain_;
    };

    class PlainLogger {
    public:
        PlainLogger(Logger& logger, const std::string& domain)
            : logger_(logger)
            , domain_(domain) {}

        template<typename... Args>
        void ws_request(const Args&... args) {
            logger_.log(domain_, PlainLogLevel::WS_REQUEST, args...);
        }

        template<typename... Args>
        void ws_response(const Args&... args) {
            logger_.log(domain_, PlainLogLevel::WS_RESPONSE, args...);
        }

        template<typename... Args>
        void ws_broadcast(const Args&... args) {
            logger_.log(domain_, PlainLogLevel::WS_BROADCAST, args...);
        }

        template<typename... Args>
        void curl_request(const Args&... args) {
            logger_.log(domain_, PlainLogLevel::CURL_REQUEST, args...);
        }

        template<typename... Args>
        void curl_response(const Args&... args) {
            logger_.log(domain_, PlainLogLevel::CURL_RESPONSE, args...);
        }

    private:
        Logger& logger_;
        std::string domain_;
    };

    DomainLogger infra() { return DomainLogger(*this, "INFRA"); }
    DomainLogger strategy() { return DomainLogger(*this, "STRAT"); }
    PlainLogger plain() { return PlainLogger(*this, "PLAIN"); }
};

// Singleton wrapper for Logger
class LoggerSingleton {
public:
    static void initialize(const std::string& log_root, const std::string& strat_config_path) {
        auto config_name = generate_config_name(strat_config_path);
        auto log_dir = make_log_dir(log_root, config_name);
        auto log_filepath = generate_log_filepath(log_dir, config_name);
        instance = std::make_unique<Logger>(log_filepath);
    }

    static Logger& get() {
        if(!instance) {
            throw std::runtime_error("Logger is not initialized. Call initialize() first.");
        }
        return *instance;
    }

private:
    LoggerSingleton() = default;
    ~LoggerSingleton() = default;

    LoggerSingleton(const LoggerSingleton&) = delete;
    LoggerSingleton& operator=(const LoggerSingleton&) = delete;

    static std::string generate_config_name(const std::string& strategy_config_path) {
        std::filesystem::path config_path{strategy_config_path};
        return config_path.stem().string(); // Extract filename without extension
    }

    static std::string make_log_dir(const std::string& log_root, const std::string& config_name) {
        std::filesystem::path log_dir{log_root};
        log_dir /= config_name;
        std::filesystem::create_directories(log_dir);
        return log_dir.string();
    }

    static std::string generate_log_filepath(const std::string& log_dir, const std::string& config_name) {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::ostringstream timestamp;
        timestamp << std::put_time(std::localtime(&time_t_now), "%Y%m%d_%H%M%S") << "_" << std::setfill('0')
                  << std::setw(3) << ms.count();

        return log_dir + "/" + timestamp.str() + "_" + config_name + ".log";
    }

    static std::unique_ptr<Logger> instance;
};

std::unique_ptr<Logger> LoggerSingleton::instance;
// Strategy debug logging
#ifdef DEBUG_MODE
#define LOG_STRATEGY_DEBUG(...) LoggerSingleton::get().strategy().debug(__VA_ARGS__)
#else
#define LOG_STRATEGY_DEBUG(...) ((void)0)
#endif

// Plain debug logging
#ifdef DEBUG_MODE
#define LOG_PLAIN_DEBUG(...) LoggerSingleton::get().plain().debug(__VA_ARGS__)
#else
#define LOG_PLAIN_DEBUG(...) ((void)0)
#endif

// Infra debug logging
#ifdef DEBUG_MODE
#define LOG_INFRA_DEBUG(...) LoggerSingleton::get().infra().debug(__VA_ARGS__)
#else
#define LOG_INFRA_DEBUG(...) ((void)0)
#endif
