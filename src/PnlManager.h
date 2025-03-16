#pragma once

#include "../utils/logger.hpp"
#include <cmath>
#include <concepts>
#include <cstdint>
#include <stdexcept>

template<typename T>
concept BookConcept = requires(const T& book) {
    { book.getMid() } -> std::convertible_to<double>;
};

template<BookConcept Book>
class PnlManager {
public:
    explicit PnlManager(const Book& hedge_book)
        : position_{0.0}
        , average_cost_{0.0}
        , realized_pnl_{0.0}
        , maker_fee_{0.0}
        , taker_fee_{0.0}
        , hedge_book_(hedge_book) {}

    ~PnlManager() = default;

    PnlManager(const PnlManager&) = delete;
    PnlManager& operator=(const PnlManager&) = delete;
    PnlManager(PnlManager&&) = delete;
    PnlManager& operator=(PnlManager&&) = delete;

    void add_trade(double quantity, double price, double fee, bool is_maker) {
        validate_inputs(quantity, price);

        if(is_maker) {
            maker_fee_ += fee;
        } else {
            taker_fee_ += fee;
        }

        if(is_increasing_position(quantity)) {
            handle_position_increase(quantity, price);
        } else {
            handle_position_decrease(quantity, price);
        }
    }

    [[nodiscard]] double get_unrealized_pnl() const {

        if(position_ == 0.) {
            LOG_STRATEGY_DEBUG([&]() {
                std::string result;
                result += f("action", "get_unrealized_pnl") + " ";
                result += f("position", position_) + " ";
                result += f("unrealized_pnl", 0.0);
                return result;
            }());
            return 0.0;
        }

        const double current_mid = hedge_book_.getMid();

        if(is_long_position()) {
            LOG_STRATEGY_DEBUG([&]() {
                std::string result;
                result += f("action", "get_unrealized_pnl") + " ";
                result += f("is_long_position", true) + " ";
                result += f<Precision::Full>("current_mid", current_mid) + " ";
                result += f<Precision::Full>("average_cost", average_cost_) + " ";
                result += f<Precision::Full>("position", position_) + " ";
                result += f("formula", "(current_mid - average_cost_) * std::abs(position_)") + " ";
                result += f<Precision::Full>("unrealized_pnl", (current_mid - average_cost_) * std::abs(position_));
                return result;
            }());
            return (current_mid - average_cost_) * std::abs(position_);
        } else {
            LOG_STRATEGY_DEBUG([&]() {
                std::string result;
                result += f("action", "get_unrealized_pnl") + " ";
                result += f("is_long_position", false) + " ";
                result += f<Precision::Full>("current_mid", current_mid) + " ";
                result += f<Precision::Full>("average_cost", average_cost_) + " ";
                result += f<Precision::Full>("position", position_) + " ";
                result += f("formula", "(average_cost_ - current_mid) * std::abs(position_)") + " ";
                result += f<Precision::Full>("unrealized_pnl", (average_cost_ - current_mid) * std::abs(position_));
                return result;
            }());
            return (average_cost_ - current_mid) * std::abs(position_);
        }
    }
    [[nodiscard]] double get_realized_pnl() const { return realized_pnl_; }
    [[nodiscard]] double get_total_pnl() const { return get_realized_pnl() + get_unrealized_pnl(); }

    [[nodiscard]] double get_realized_pnl_with_fee() const {
        return get_realized_pnl() - (get_maker_fee() + get_taker_fee());
    }
    [[nodiscard]] double get_total_pnl_with_fee() const { return get_realized_pnl_with_fee() + get_unrealized_pnl(); }

    [[nodiscard]] const Book& get_hedge_book() const { return hedge_book_; }
    [[nodiscard]] double get_position() const { return position_; }
    [[nodiscard]] double get_average_cost() const { return average_cost_; }
    [[nodiscard]] double get_maker_fee() const { return maker_fee_; }
    [[nodiscard]] double get_taker_fee() const { return taker_fee_; }

    void adjust_state(double new_position,
                      double new_average_cost,
                      double new_realized_pnl,
                      double new_maker_fee,
                      double new_taker_fee) {
        position_ = new_position;
        average_cost_ = new_average_cost;
        realized_pnl_ = new_realized_pnl;
        maker_fee_ = new_maker_fee;
        taker_fee_ = new_taker_fee;
    }

private:
    double position_; // Positive for long, negative for short
    double average_cost_;
    double realized_pnl_;
    double maker_fee_;
    double taker_fee_;
    const Book& hedge_book_;

    [[nodiscard]] bool is_increasing_position(double quantity) const {
        return (position_ >= 0 && quantity > 0) || (position_ <= 0 && quantity < 0);
    }

    [[nodiscard]] bool will_flip_position(double quantity) const { return std::abs(quantity) > std::abs(position_); }

    [[nodiscard]] bool is_long_position() const { return position_ > 0; }

    void handle_position_increase(double quantity, double price) {
        average_cost_ = calculate_new_average_cost(quantity, price);
        position_ += quantity;
    }

    void handle_position_decrease(double quantity, double price) {
        if(will_flip_position(quantity)) {
            handle_position_flip(quantity, price);
        } else {
            handle_partial_close(quantity, price);
        }
    }

    void handle_position_flip(double quantity, double price) {
        const double close_qty = -position_;
        realize_pnl(close_qty, price);

        const double new_qty = quantity + position_;
        position_ = new_qty;
        average_cost_ = price;
    }

    void handle_partial_close(double quantity, double price) {
        realize_pnl(quantity, price);
        position_ += quantity;

        if(is_position_closed()) {
            reset_position();
        }
    }

    void realize_pnl(double close_qty, double price) {
        if(is_long_position()) {
            realized_pnl_ += (price - average_cost_) * std::abs(close_qty);
        } else {
            realized_pnl_ += (average_cost_ - price) * std::abs(close_qty);
        }
    }

    void reset_position() { average_cost_ = 0; }

    [[nodiscard]] bool is_position_closed() const { return position_ == 0; }

    [[nodiscard]] double calculate_new_average_cost(double new_qty, double new_price) const {
        const double total_cost = (position_ * average_cost_) + (new_qty * new_price);
        const double total_quantity = position_ + new_qty;
        return total_quantity != 0 ? total_cost / total_quantity : 0.0;
    }

    void validate_inputs(double quantity, double price) const {
        if(price <= 0) {
            throw std::invalid_argument("Price must be positive");
        }
        if(quantity == 0) {
            throw std::invalid_argument("Quantity cannot be zero");
        }
    }
};
