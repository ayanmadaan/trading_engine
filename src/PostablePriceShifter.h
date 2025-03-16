#pragma once

#include "Side.h"
#include <vector>

class PostablePriceShifter {
public:
    explicit PostablePriceShifter(double ticks_from_postable, double tick_size)
        : ticks_from_postable_(ticks_from_postable)
        , tick_size_(tick_size) {}

    template<Side::Type SideType>
    void shift(std::vector<double>& prices, double market_opposite_price) const {
        if(prices.empty()) return;

        constexpr Side SIDE(SideType);

        if(SIDE.is_inner_or_equal(prices[0], market_opposite_price)) {
            prices[0] = SIDE.add_away(market_opposite_price, (1.0 + ticks_from_postable_) * tick_size_);
        }

        for(size_t i = 1; i < prices.size(); ++i) {
            if(SIDE.is_inner_or_equal(prices[i], prices[i - 1])) {
                prices[i] = SIDE.add_away(prices[i - 1], tick_size_);
            }
        }
    }

    void shift_asks(std::vector<double>& prices, double market_bid) const {
        shift<Side::Type::Ask>(prices, market_bid);
    }

    void shift_bids(std::vector<double>& prices, double market_ask) const {
        shift<Side::Type::Bid>(prices, market_ask);
    }

private:
    double ticks_from_postable_;
    double tick_size_;
};
