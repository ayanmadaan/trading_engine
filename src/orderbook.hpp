#pragma once
#include <iomanip>
#include <map>
#include <sstream>

struct OrderBook {
public:
    std::string toString() const {
        std::stringstream ss;
        ss << "OrderBook Timestamp: " << m_timestamp << "\n";

        ss << "Bid Levels:\n";
        for(const auto& bid : m_bidLevels) {
            ss << std::fixed << std::setprecision(6) // Format output with 6 decimal precision
               << "Price: " << bid.first << ", Quantity: " << bid.second << "\n";
        }

        ss << "Ask Levels:\n";
        for(const auto& ask : m_askLevels) {
            ss << std::fixed << std::setprecision(6) // Format output with 6 decimal precision
               << "Price: " << ask.first << ", Quantity: " << ask.second << "\n";
        }

        return ss.str();
    }

    long long m_timestamp;
    std::map<double, double, std::greater<double>> m_bidLevels;
    std::map<double, double> m_askLevels;
};
