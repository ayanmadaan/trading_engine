#pragma once

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>

using Clock = std::chrono::system_clock;

enum class Precision : std::uint8_t { One = 1, Two, Three, Four, Five, Six, Seven, Eight, Nine, Ten, Full };

template<Precision P = Precision::Six, typename T>
static std::string f(const std::string& key, const T& value) {
    std::stringstream ss;
    // For floating-point types, set precision based on the Precision enum.
    if constexpr(std::is_floating_point_v<T>) {
        if constexpr(P == Precision::Full) {
            ss << std::fixed << std::setprecision(std::numeric_limits<T>::max_digits10);
        } else {
            ss << std::fixed << std::setprecision(static_cast<int>(P));
        }
    }
    ss << std::boolalpha;
    ss << key << "=";
    // If T is convertible to std::string, check for spaces and add quotes if needed.
    if constexpr(std::is_convertible<T, std::string>::value) {
        std::string s_value = value;
        if(s_value.find(' ') != std::string::npos) {
            ss << "\"" << s_value << "\"";
        } else {
            ss << s_value;
        }
    } else {
        ss << value;
    }
    return ss.str();
}

// Format Clock::duration as "XhYmZsNms"
static std::string format_duration(const Clock::duration& duration) {
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    auto remainder = duration - hours;
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(remainder);
    remainder -= minutes;
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(remainder);
    remainder -= seconds;
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(remainder);

    std::stringstream ss;
    bool needs_separator = false;

    if(hours.count() > 0) {
        ss << hours.count() << "h";
        needs_separator = true;
    }
    if(needs_separator || minutes.count() > 0) {
        ss << minutes.count() << "m";
        needs_separator = true;
    }
    if(needs_separator || seconds.count() > 0) {
        ss << seconds.count() << "s";
        needs_separator = true;
    }
    if(milliseconds.count() > 0) {
        ss << milliseconds.count() << "ms";
    }

    if(ss.str().empty()) {
        return "0ms";
    }
    return ss.str();
}

// Format system_clock::time_point as "YYYYMMDD_HHMMSS_NNNNNN"
static std::string format_time_point_custom(const Clock::time_point& time_point) {
    // Convert to time_t for date/time components
    std::time_t tt = Clock::to_time_t(time_point);
    std::tm* tm = std::localtime(&tt);
    if(!tm) {
        throw std::runtime_error("Failed to convert time to local time");
    }

    // Get microseconds
    auto microseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(time_point.time_since_epoch() % std::chrono::seconds(1));

    std::stringstream ss;
    ss << std::setfill('0') << std::setw(4) << (tm->tm_year + 1900) // Year
       << std::setw(2) << (tm->tm_mon + 1) // Month (0-based)
       << std::setw(2) << tm->tm_mday // Day
       << "_" << std::setw(2) << tm->tm_hour // Hour
       << std::setw(2) << tm->tm_min // Minute
       << std::setw(2) << tm->tm_sec // Second
       << "_" << std::setw(6) << microseconds.count(); // Microseconds

    return ss.str();
}

// Format std::chrono::system_clock::time_point as "YYYY-MM-DDTHH:MM:SS.NNNNNN"
static std::string format_time_point_iso8601(const Clock::time_point& time_point) {
    // Convert to time_t for date/time components
    std::time_t tt = std::chrono::system_clock::to_time_t(time_point);

    // Use gmtime for UTC (ISO 8601 standard)
    std::tm* utc_tm = std::gmtime(&tt);
    if(!utc_tm) {
        throw std::runtime_error("Failed to convert time to UTC");
    }

    // Get microseconds
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(time_point.time_since_epoch()) % 1000000;

    // Format the string
    std::ostringstream ss;
    ss << std::put_time(utc_tm, "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(6) << micros.count() << 'Z'; // UTC indicator

    return ss.str();
}

// Format nanosecond timestamp as "YYYY-MM-DDTHH:MM:SS.NNNNNN"
static std::string format_ns_iso8601(uint64_t ns) {
    using namespace std::chrono;
    // Create a time_point from nanoseconds using system_clock for conversion.
    auto tp = time_point_cast<nanoseconds>(system_clock::time_point(nanoseconds(ns)));

    // Extract the whole seconds portion.
    auto secs = time_point_cast<seconds>(tp);

    // Calculate the remaining microseconds (6 digits precision).
    auto micros = duration_cast<microseconds>(tp - secs);

    // Convert the seconds portion to a time_t for formatting.
    std::time_t t = system_clock::to_time_t(tp);
    std::tm tm_time;

    // Use the thread-safe gmtime_r for Ubuntu.
    gmtime_r(&t, &tm_time);

    // Format the time to ISO 8601: "YYYY-MM-DDTHH:MM:SS.NNNNNN"
    std::stringstream ss;
    ss << std::put_time(&tm_time, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(6) << std::setfill('0') << micros.count();

    return ss.str();
}
