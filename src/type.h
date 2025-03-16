#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

enum class Exchange : std::uint8_t { Binance, Bybit, Okx };
[[nodiscard]]
inline std::string exchange_to_string(Exchange exchange) {
    switch(exchange) {
    case Exchange::Binance: return "binance";
    case Exchange::Bybit: return "bybit";
    case Exchange::Okx: return "okx";
    default: throw std::invalid_argument("Unknown exchange");
    }
}

enum class TradingMode : std::uint8_t { Live, Mock };
[[nodiscard]]
inline std::string trading_mode_to_string(TradingMode mode) {
    switch(mode) {
    case TradingMode::Live: return "live";
    case TradingMode::Mock: return "mock";
    default: throw std::invalid_argument("Unknown trading mode");
    }
}

enum class VenueRole : std::uint8_t { Quote, Hedge, Reference };
[[nodiscard]]
inline std::string venue_role_to_string(VenueRole role) {
    switch(role) {
    case VenueRole::Quote: return "quote";
    case VenueRole::Hedge: return "hedge";
    case VenueRole::Reference: return "reference";
    default: throw std::invalid_argument("Unknown order role");
    }
}

enum class OrderType : std::uint8_t { Limit, Market, PostOnly };
[[nodiscard]]
inline std::string order_type_to_string(OrderType type) {
    switch(type) {
    case OrderType::Limit: return "limit";
    case OrderType::Market: return "market";
    case OrderType::PostOnly: return "post_only";
    default: throw std::invalid_argument("Unknown order type");
    }
}
