#pragma once

#include "Side.h"
#include "logging.h"

template<typename Book, typename QuoteMidService, typename TargetOrderManager>
class OrderHealthChecker {
public:
    template<typename Book_ = Book,
             typename QuoteMidService_ = QuoteMidService,
             typename TargetOrderManager_ = TargetOrderManager>
    explicit OrderHealthChecker(double minimum_distance,
                                const Book_& reference_book,
                                const QuoteMidService_& quote_mid_service,
                                const TargetOrderManager_& target_order_manager)
        : minimum_distance_{minimum_distance}
        , reference_book_{reference_book}
        , quote_mid_service_{quote_mid_service}
        , target_order_manager_{target_order_manager} {}

    template<Side::Type SideType>
    [[nodiscard]] bool check() const noexcept {
        constexpr Side side(SideType);

        // Use the templated getter to simplify the code
        const bool has_orders = !target_order_manager_.template get_target_orders<SideType>().empty();

        if(!has_orders) {
            LOG_STRATEGY_DEBUG([&]() {
                return "[OrderHealthChecker] " + f("action", "check_order_health") + " " + f("side", side.to_string()) +
                       " " + f("reason", "no_active_orders");
            }());
            return false;
        }

        const auto quote = get_quote_touch_price<SideType>();
        const auto boundary = quote * side.add_inner(1., minimum_distance_);
        const auto ref_touch = get_shifted_reference_touch_price<SideType>();
        const bool is_safe = side.is_inner(ref_touch, boundary);

        LOG_STRATEGY_DEBUG([&]() {
            return "[OrderHealthChecker] " + f("action", "check_order_health") + " " + f("side", side.to_string()) +
                   " " + f("reason", "safe") + " " + f("best_quote_price", quote) + " " +
                   f("minimum_distance_bps", minimum_distance_ * 1.0E4) + " " + f("safety_boundary", boundary) + " " +
                   f("ref_touch_price", ref_touch) + " " +
                   f("distance_bps", calculate_distance<SideType>(quote, ref_touch) * 1.0E4) + " " +
                   f("minimum_distance < distance", minimum_distance_ < calculate_distance<SideType>(quote, ref_touch));
        }());

        return is_safe;
    }

    // Helper template function to calculate distance
    template<Side::Type SideType>
    inline double calculate_distance(double quote, double ref_touch) const noexcept {
        if constexpr(SideType == Side::Type::Ask) {
            return -((ref_touch - quote) / quote);
        } else {
            return ((ref_touch - quote) / quote);
        }
    }

private:
    template<Side::Type SideType>
    [[nodiscard]] double get_reference_touch_price() const {

        LOG_STRATEGY_DEBUG([&]() {
            const auto price =
                (SideType == Side::Type::Ask) ? reference_book_.getBestAsk() : reference_book_.getBestBid();
            const auto spread = reference_book_.getBestAsk() - reference_book_.getBestBid();
            const auto mid = reference_book_.getMid();

            return "[OrderHealthChecker] " + f("action", "retrieve_reference_touch_price") + " " +
                   f("side", Side(SideType).to_string()) + " " + f("touch_price", price) + " " + f("spread", spread) +
                   " " + f("mid_price", mid) + " " + f("spread_bps", (spread / mid) * 1.0E4);
        }());

        if constexpr(SideType == Side::Type::Ask) {
            return reference_book_.getBestAsk();
        } else {
            return reference_book_.getBestBid();
        }
    }

    template<Side::Type SideType>
    [[nodiscard]] double get_shifted_reference_touch_price() const {
        LOG_STRATEGY_DEBUG([&]() {
            const auto touch = get_reference_touch_price<SideType>();
            const auto shifted = quote_mid_service_.shift(touch);
            const auto shift_ratio = (shifted - touch) / touch;

            return "[OrderHealthChecker] " + f("action", "shift_reference_touch") + " " +
                   f("side", Side(SideType).to_string()) + " " + f("original_touch", touch) + " " +
                   f("shifted_touch", shifted) + " " + f("shift_ratio_bps", shift_ratio * 1.0E4);
        }());

        return quote_mid_service_.shift(get_reference_touch_price<SideType>());
    }

    template<Side::Type SideType>
    [[nodiscard]] double get_quote_touch_price() const {
        if constexpr(SideType == Side::Type::Ask) {
            return target_order_manager_.get_ask_target_orders().begin()->first;
        } else {
            return target_order_manager_.get_bid_target_orders().begin()->first;
        }
    }

    double minimum_distance_;
    const Book& reference_book_;
    const QuoteMidService& quote_mid_service_;
    const TargetOrderManager& target_order_manager_;
};
