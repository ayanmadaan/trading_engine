#pragma once

#include "../utils/logger.hpp"
#include "format.h"
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string>

enum class ActionStatus : std::uint8_t {
    Pass,
    Fail,
    Attempted,
};

inline std::string action_status_to_string(ActionStatus status) {
    switch(status) {
    case ActionStatus::Pass: return "pass";
    case ActionStatus::Fail: return "fail";
    case ActionStatus::Attempted: return "attempted";
    default: return "unknown";
    }
}

template<LogLevel Level = LogLevel::INFO, ActionStatus Status, typename... Args>
inline void log_action(const std::string& action, Args&&... args) {
#ifdef DEBUG_MODE
    if constexpr(Level == LogLevel::DEBUG) {
        std::string message = f("action", action) + " " + f("status", action_status_to_string(Status));
        (
            [&message](const auto& arg) {
                if constexpr(std::is_convertible_v<decltype(arg), std::string>) {
                    message += " " + std::string(arg);
                } else if constexpr(std::is_arithmetic_v<std::remove_reference_t<decltype(arg)>>) {
                    message += " " + std::to_string(arg);
                } else {
                    message += " " + std::string("(non-string type)");
                }
            }(std::forward<Args>(args)),
            ...);
        LOG_STRATEGY_DEBUG(message);
    }
#endif

    if constexpr(Level != LogLevel::DEBUG) {
        std::string message = f("action", action) + " " + f("status", action_status_to_string(Status));
        (
            [&message](const auto& arg) {
                if constexpr(std::is_convertible_v<decltype(arg), std::string>) {
                    message += " " + std::string(arg);
                } else if constexpr(std::is_arithmetic_v<std::remove_reference_t<decltype(arg)>>) {
                    message += " " + std::to_string(arg);
                } else {
                    message += " " + std::string("(non-string type)");
                }
            }(std::forward<Args>(args)),
            ...);

        if constexpr(Level == LogLevel::INFO) {
            LoggerSingleton::get().strategy().info(message);
        } else if constexpr(Level == LogLevel::WARNING) {
            LoggerSingleton::get().strategy().warning(message);
        } else if constexpr(Level == LogLevel::ERROR) {
            LoggerSingleton::get().strategy().error(message);
        }
    }
}

// Helper macro to prevent argument evaluation in release mode
#ifdef DEBUG_MODE
#define LOG_ACTION_DEBUG(status, action, ...) log_action<LogLevel::DEBUG, status>(action, __VA_ARGS__)
#else
#define LOG_ACTION_DEBUG(...) // expands to nothing
#endif

// Wrapper for a passing action
template<LogLevel Level = LogLevel::INFO, typename... Args>
inline void log_action_pass(const std::string& action, Args&&... args) {
#ifdef DEBUG_MODE
    if constexpr(Level == LogLevel::DEBUG) {
        log_action<Level, ActionStatus::Pass>(action, std::forward<Args>(args)...);
        return;
    }
#endif
    if constexpr(Level != LogLevel::DEBUG) {
        log_action<Level, ActionStatus::Pass>(action, std::forward<Args>(args)...);
    }
}

// Wrapper for a failing action
template<LogLevel Level = LogLevel::INFO, typename... Args>
inline void log_action_fail(const std::string& action, const std::string& reason, Args&&... args) {
#ifdef DEBUG_MODE
    if constexpr(Level == LogLevel::DEBUG) {
        log_action<Level, ActionStatus::Fail>(action, f("reason", reason), std::forward<Args>(args)...);
        return;
    }
#endif
    if constexpr(Level != LogLevel::DEBUG) {
        log_action<Level, ActionStatus::Fail>(action, f("reason", reason), std::forward<Args>(args)...);
    }
}

// Wrapper for an attempted action
template<LogLevel Level = LogLevel::INFO, typename... Args>
inline void log_action_attempt(const std::string& action, Args&&... args) {
#ifdef DEBUG_MODE
    if constexpr(Level == LogLevel::DEBUG) {
        log_action<Level, ActionStatus::Attempted>(action, std::forward<Args>(args)...);
        return;
    }
#endif
    if constexpr(Level != LogLevel::DEBUG) {
        log_action<Level, ActionStatus::Attempted>(action, std::forward<Args>(args)...);
    }
}

// Macro versions for true zero-cost debug logging
#ifdef DEBUG_MODE
#define LOG_ACTION_PASS_DEBUG(action, ...) log_action_pass<LogLevel::DEBUG>(action, __VA_ARGS__)
#define LOG_ACTION_FAIL_DEBUG(action, reason, ...) log_action_fail<LogLevel::DEBUG>(action, reason __VA_OPT__(,) __VA_ARGS__)
#define LOG_ACTION_ATTEMPT_DEBUG(action, ...) log_action_attempt<LogLevel::DEBUG>(action, __VA_ARGS__)
#else
#define LOG_ACTION_PASS_DEBUG(...)
#define LOG_ACTION_FAIL_DEBUG(...)
#define LOG_ACTION_ATTEMPT_DEBUG(...)
#endif

template<LogLevel Level = LogLevel::INFO, typename... Args>
inline void log_event(const std::string& event, Args&&... args) {
#ifdef DEBUG_MODE
    if constexpr(Level == LogLevel::DEBUG) {
        std::string message = f("event", event);
        (
            [&message](const auto& arg) {
                if constexpr(std::is_convertible_v<decltype(arg), std::string>) {
                    message += " " + std::string(arg);
                } else if constexpr(std::is_arithmetic_v<std::remove_reference_t<decltype(arg)>>) {
                    message += " " + std::to_string(arg);
                } else {
                    message += " " + std::string("(non-string type)");
                }
            }(std::forward<Args>(args)),
            ...);
        LOG_STRATEGY_DEBUG(message);
    }
#endif

    if constexpr(Level != LogLevel::DEBUG) {
        std::string message = f("event", event);
        (
            [&message](const auto& arg) {
                if constexpr(std::is_convertible_v<decltype(arg), std::string>) {
                    message += " " + std::string(arg);
                } else if constexpr(std::is_arithmetic_v<std::remove_reference_t<decltype(arg)>>) {
                    message += " " + std::to_string(arg);
                } else {
                    message += " " + std::string("(non-string type)");
                }
            }(std::forward<Args>(args)),
            ...);

        if constexpr(Level == LogLevel::INFO) {
            LoggerSingleton::get().strategy().info(message);
        } else if constexpr(Level == LogLevel::WARNING) {
            LoggerSingleton::get().strategy().warning(message);
        } else if constexpr(Level == LogLevel::ERROR) {
            LoggerSingleton::get().strategy().error(message);
        }
    }
}

// Helper macro to prevent argument evaluation in release mode
#ifdef DEBUG_MODE
#define LOG_EVENT_DEBUG(event, ...) log_event<LogLevel::DEBUG>(event, __VA_ARGS__)
#else
#define LOG_EVENT_DEBUG(...) // expands to nothing
#endif
