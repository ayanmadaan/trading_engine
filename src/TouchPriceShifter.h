#pragma once

#include "Side.h"
#include <vector>

class TouchPriceShifter {
public:
    explicit TouchPriceShifter(double ticks_from_touch, double tick_size)
        : ticks_from_touch_(ticks_from_touch)
        , tick_size_(tick_size) {}

    template<Side::Type SideType>
    void shift(std::vector<double>& prices, double market_price) const {
        if(prices.empty()) return;

        constexpr Side SIDE(SideType);

        if(SIDE.is_inner(prices[0], market_price)) {
            prices[0] = SIDE.add_away(market_price, ticks_from_touch_ * tick_size_);
        }

        for(size_t i = 1; i < prices.size(); ++i) {
            if(SIDE.is_inner_or_equal(prices[i], prices[i - 1])) {
                prices[i] = SIDE.add_away(prices[i - 1], tick_size_);
            }
        }
    }

    void shift_asks(std::vector<double>& prices, double market_price) const {
        shift<Side::Type::Ask>(prices, market_price);
    }

    void shift_bids(std::vector<double>& prices, double market_price) const {
        shift<Side::Type::Bid>(prices, market_price);
    }

private:
    double ticks_from_touch_;
    double tick_size_;
};
