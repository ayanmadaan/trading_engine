#pragma once

#include "Side.h"
#include <cmath>
#include <stdexcept>

enum class SizeRoundMode { Ceil, Floor, Nearest };

enum class PriceRoundMode { Inner, Away, Nearest };

inline SizeRoundMode parse_size_round_mode(const std::string& mode) {
    if(mode == "ceil") return SizeRoundMode::Ceil;
    if(mode == "floor") return SizeRoundMode::Floor;
    if(mode == "nearest") return SizeRoundMode::Nearest;
    throw std::runtime_error("Invalid size round mode: " + mode);
}

inline PriceRoundMode parse_price_round_mode(const std::string& mode) {
    if(mode == "inner") return PriceRoundMode::Inner;
    if(mode == "away") return PriceRoundMode::Away;
    if(mode == "nearest") return PriceRoundMode::Nearest;
    throw std::runtime_error("Invalid price round mode: " + mode);
}

class BaseRounder {
public:
    explicit BaseRounder(double tick_size)
        : tick_size_(tick_size) {
        if(tick_size <= 0.0) {
            throw std::invalid_argument("Tick size must be positive");
        }
    }

protected:
    [[nodiscard]]
    double round_up(double value) const noexcept {
        return std::ceil(value / tick_size_) * tick_size_;
    }

    [[nodiscard]]
    double round_down(double value) const noexcept {
        return std::floor(value / tick_size_) * tick_size_;
    }

    [[nodiscard]]
    double round_nearest(double value) const noexcept {
        return std::round(value / tick_size_) * tick_size_;
    }

    double tick_size_;
};

class SizeRounder : public BaseRounder {
public:
    SizeRounder(double min_size, SizeRoundMode mode)
        : BaseRounder(min_size)
        , mode_(mode) {}

    [[nodiscard]]
    double round(double size) const noexcept {
        double rounded = size;
        switch(mode_) {
        case SizeRoundMode::Ceil: rounded = round_up(size); break;
        case SizeRoundMode::Floor: rounded = round_down(size); break;
        case SizeRoundMode::Nearest: rounded = round_nearest(size); break;
        }
        return std::max(rounded, tick_size_);
    }

private:
    SizeRoundMode mode_;
};

class PriceRounder : public BaseRounder {
public:
    PriceRounder(double tick_size, PriceRoundMode mode)
        : BaseRounder(tick_size)
        , mode_(mode) {}

    template<Side::Type SideType>
    [[nodiscard]]
    double round_price(double price) const noexcept {
        switch(mode_) {
        case PriceRoundMode::Inner: return Side::sign(SideType) < 0 ? round_up(price) : round_down(price);
        case PriceRoundMode::Away: return Side::sign(SideType) < 0 ? round_down(price) : round_up(price);
        case PriceRoundMode::Nearest: return round_nearest(price);
        default: return price;
        }
    }

    [[nodiscard]]
    double round_ask(double price) const noexcept {
        return round_price<Side::Type::Ask>(price);
    }

    [[nodiscard]]
    double round_bid(double price) const noexcept {
        return round_price<Side::Type::Bid>(price);
    }

private:
    PriceRoundMode mode_;
};
