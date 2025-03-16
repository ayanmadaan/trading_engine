#pragma once

#include "../infra/book.hpp"
#include "Side.h"
#include "book_healthchecks.h"
#include "format.h"
#include "logging.h"

#include <cmath>

template<typename HedgeExecutor, typename QuotePositionManagerType, typename HedgePositionManagerType>
class Hedger {
public:
    explicit Hedger(HedgeExecutor& hedge_executor,
                    const QuotePositionManagerType& quote_position_manager,
                    const HedgePositionManagerType& hedge_position_manager,
                    const Book& hedge_book,
                    const std::string& instrument,
                    double min_hedge_size,
                    uint64_t stale_threshold_ns,
                    double max_spread)
        : hedge_executor_(hedge_executor)
        , quote_position_manager_(quote_position_manager)
        , hedge_position_manager_(hedge_position_manager)
        , hedge_book_{hedge_book}
        , instrument_(instrument)
        , min_hedge_size_{min_hedge_size}
        , stale_threshold_ns_{stale_threshold_ns}
        , max_spread_{max_spread} {}

    [[nodiscard]] std::pair<bool, std::string> healthcheck() const {
        if(!spread_checker_.check(hedge_book_)) {
            LOG_ACTION_FAIL_DEBUG("check_hedger_health", "hedge_market_illiquid");
            return {false, "hedge_market_illiquid"};
        } else if(!freshness_checker_.check(hedge_book_)) {
            LOG_ACTION_FAIL_DEBUG("check_hedger_health", "hedge_book_outdated");
            return {false, "hedge_book_outdated"};
        } else if(!hedge_executor_.isWebSocketReady()) {
            LOG_ACTION_FAIL_DEBUG("check_hedger_health", "hedge_ws_disconnected");
            return {false, "hedge_ws_disconnected"};
        }
        return {true, ""};
    }

    void hedge() {
        const auto total_exposure = calculate_total_exposure();

        if(!is_exposure_significant(total_exposure)) {
            LOG_ACTION_PASS_DEBUG("hedge",
                                  f("reason", "total_exposure_within_min_hedge_size"),
                                  f("total_exposure", total_exposure),
                                  f("min_hedge_size", min_hedge_size_));
            return;
        }

        const auto unhedged_exposure = calculate_unhedged_exposure(total_exposure);
        if(!is_exposure_significant(unhedged_exposure)) {
            LOG_ACTION_PASS_DEBUG("hedge",
                                  f("reason", "unhedged_exposure_within_min_hedge_size"),
                                  f("unhedged_exposure", unhedged_exposure),
                                  f("min_hedge_size", min_hedge_size_));
            return;
        }

        const auto hedge_side = determine_hedge_side(unhedged_exposure);
        send_hedge_order(std::abs(unhedged_exposure), hedge_side);
        log_action_attempt(
            "hedge", f("unhedged_exposure", std::abs(unhedged_exposure)), f("side", hedge_side.to_string()));
    }

private:
    [[nodiscard]] bool is_exposure_significant(double exposure) const { return std::abs(exposure) >= min_hedge_size_; }
    [[nodiscard]] double get_quote_position() const { return quote_position_manager_.get_position(); }
    [[nodiscard]] double get_hedge_position() const { return hedge_position_manager_.get_position(); }
    [[nodiscard]] double calculate_total_exposure() const { return get_quote_position() + get_hedge_position(); }
    [[nodiscard]] double get_potential_fill_size(Side::Type side) const {
        const auto& pending_orders = hedge_executor_.getOrdersByStatus(OrderStatus::PENDING);

        double total_size{0.};

        for(auto order : pending_orders) {
            if(order->m_side == (side == Side::Type::Bid)) {
                total_size += order->m_qtySubmitted;
            }
        }

        const auto& live_orders = hedge_executor_.getOrdersByStatus(OrderStatus::LIVE);
        for(auto order : live_orders) {
            if(order->m_side == (side == Side::Type::Bid)) {
                total_size += order->m_qtyOnExch;
            }
        }

        const auto& partially_filled_orders = hedge_executor_.getOrdersByStatus(OrderStatus::PARTIALLY_FILLED);
        for(auto order : partially_filled_orders) {
            if(order->m_side == (side == Side::Type::Bid)) {
                total_size += order->m_qtyOnExch;
            }
        }

        return total_size;
    }
    [[nodiscard]] double calculate_unhedged_exposure(double exposure) const {
        if(exposure > 0.) {
            const auto potential_ask_fills = get_potential_fill_size(Side::ask());
            return exposure > potential_ask_fills ? exposure - potential_ask_fills : 0.;
        }

        if(exposure < 0.) {
            const auto potential_bid_fills = get_potential_fill_size(Side::bid());
            return (-exposure) > potential_bid_fills ? exposure + potential_bid_fills : 0.;
        }

        return 0.;
    }
    [[nodiscard]] Side determine_hedge_side(double exposure) const { return exposure > 0. ? Side::ask() : Side::bid(); }
    [[nodiscard]] const std::string& get_instrument() const { return instrument_; }

    void send_hedge_order(double size, Side side) const {
        const auto order_id = hedge_executor_.placeOrder(instrument_, 0.0, size, side == Side::bid(), "market");
        log_action_attempt("send_hedge",
                           f("client_order_id", order_id),
                           f("role", "hedge"),
                           f("instrument", instrument_),
                           f("price", "market"),
                           f("size", size),
                           f("side", side == Side::bid() ? "bid" : "ask"),
                           f("order_type", "market"));
    }

    bool is_good_{false};
    HedgeExecutor& hedge_executor_;
    const QuotePositionManagerType& quote_position_manager_;
    const HedgePositionManagerType& hedge_position_manager_;
    const Book& hedge_book_;
    const std::string& instrument_;
    const double min_hedge_size_;
    double max_spread_;
    uint64_t stale_threshold_ns_;

    BookSpreadChecker spread_checker_{max_spread_};
    BookFreshnessChecker freshness_checker_{stale_threshold_ns_};
};
