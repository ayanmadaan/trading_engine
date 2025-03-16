#pragma once

#include "../utils/logger.hpp"
#include "PostablePriceShifter.h"
#include "TouchPriceShifter.h"
#include "rounding.h"
#include <cmath>

template<typename Book, typename QuoteMidServiceType>
class TargetOrderManager {
public:
    using Price = double;
    using Size = double;

    enum class OffsetBase : uint8_t { Mid, Touch };

    struct Config {
        double price_tick_size;
        double quantity_tick_size;
        PriceRoundMode price_round_mode;
        SizeRoundMode size_round_mode;
        bool enable_touch_price;
        double ticks_from_touch;
        bool enable_postable_price;
        double ticks_from_postable;
        OffsetBase offset_base;

        Config(double price_tick_size,
               double quantity_tick_size,
               PriceRoundMode price_round_mode,
               SizeRoundMode size_round_mode,
               bool enable_touch_price,
               double ticks_from_touch,
               bool enable_postable_price,
               double ticks_from_postable,
               OffsetBase offset_base)
            : price_tick_size(price_tick_size)
            , quantity_tick_size(quantity_tick_size)
            , price_round_mode(price_round_mode)
            , size_round_mode(size_round_mode)
            , enable_touch_price(enable_touch_price)
            , ticks_from_touch(ticks_from_touch)
            , enable_postable_price(enable_postable_price)
            , ticks_from_postable(ticks_from_postable)
            , offset_base(offset_base) {
            validate();
        }

    private:
        void validate() const {
            if(price_tick_size <= 0.0) {
                throw std::invalid_argument("Price tick size must be positive");
            }
            if(quantity_tick_size <= 0.0) {
                throw std::invalid_argument("Quantity tick size must be positive");
            }
        }
    };
    ;

    struct OrderConfig {
        const double price_offset;
        const Size size;

        OrderConfig(double offset, Size sz)
            : price_offset(offset)
            , size(sz) {
            if(price_offset <= 0.0) {
                throw std::invalid_argument("Price offset must be positive");
            }
            if(sz <= 0.0) {
                throw std::invalid_argument("Size must be positive");
            }
        }
    };

    struct TargetOrder {
        Side::Type side;
        Price price;
        Size size;
    };

    // Comparator for ask orders (ascending order)
    struct AskComparator {
        double tolerance;
        AskComparator(double tol = 0.0)
            : tolerance(tol) {}

        bool operator()(const double& lhs, const double& rhs) const {
            // If the prices are within tolerance, treat them as equal
            if(std::fabs(lhs - rhs) < tolerance) {
                return false;
            }
            return lhs < rhs;
        }
    };

    // Comparator for bid orders (descending order)
    struct BidComparator {
        double tolerance;
        BidComparator(double tol = 0.0)
            : tolerance(tol) {}

        bool operator()(const double& lhs, const double& rhs) const {
            // If the prices are within tolerance, treat them as equal
            if(std::fabs(lhs - rhs) < tolerance) {
                return false;
            }
            return lhs > rhs;
        }
    };

    using AskTargetOrders = std::map<double, TargetOrder, AskComparator>;
    using BidTargetOrders = std::map<double, TargetOrder, BidComparator>;

    template<typename Book_ = Book, typename QuoteMidServiceType_ = QuoteMidServiceType>
    TargetOrderManager(const Book_& quote_book,
                       const Book_& reference_book,
                       const Config& config,
                       std::vector<OrderConfig>&& order_configs,
                       const QuoteMidServiceType_& quote_mid_service)
        : quote_book_{quote_book}
        , reference_book_{reference_book}
        , config_{config}
        , order_configs_{std::move(order_configs)}
        , quote_mid_service_{quote_mid_service}
        , price_rounder_{config.price_tick_size, config.price_round_mode}
        , size_rounder_{config.quantity_tick_size, config.size_round_mode}
        , touch_price_shifter_{config.ticks_from_touch, config.price_tick_size}
        , postable_price_shifter_{config.ticks_from_postable, config.price_tick_size}
        , prices_(order_configs_.size())
        , sizes_{make_sizes(order_configs_, size_rounder_)}
        , ask_target_orders_{AskComparator(config.price_tick_size)}
        , bid_target_orders_{BidComparator(config.price_tick_size)} {}

    void refresh_ask_target_orders() { refresh_target_orders<Side::Type::Ask>(); }

    void refresh_bid_target_orders() { refresh_target_orders<Side::Type::Bid>(); }

    [[nodiscard]] const AskTargetOrders& get_ask_target_orders() const { return ask_target_orders_; }
    [[nodiscard]] const BidTargetOrders& get_bid_target_orders() const { return bid_target_orders_; }

    template<Side::Type SideType>
    [[nodiscard]] bool is_dirty() const {
        if constexpr(SideType == Side::Type::Ask) {
            return is_ask_dirty_;
        } else {
            return is_bid_dirty_;
        }
    }

    template<Side::Type SideType>
    [[nodiscard]] bool is_clean() const {
        return !is_dirty<SideType>();
    }

    template<Side::Type SideType>
    void set_dirty() {
        if constexpr(SideType == Side::Type::Ask) {
            is_ask_dirty_ = true;
        } else {
            is_bid_dirty_ = true;
        }
    }

    template<Side::Type SideType>
    void set_clean() {
        if constexpr(SideType == Side::Type::Ask) {
            is_ask_dirty_ = false;
        } else {
            is_bid_dirty_ = false;
        }
    }

    template<Side::Type SideType>
    [[nodiscard]] bool is_in_target_orders(Price price, Size size) const {
        LOG_STRATEGY_DEBUG([&]() {
            return "[TargetOrderManager] " + f("action", "is_in_target_orders") + " " +
                   f("side", SideType == Side::Type::Ask ? "Ask" : "Bid") + " " + f("price", std::to_string(price)) +
                   " " + f("size", size);
        });

        if constexpr(SideType == Side::Type::Ask) {
            LOG_STRATEGY_DEBUG([&]() {
                return "[TargetOrderManager] " + f("action", "is_in_target_orders") + " " +
                       f("all_ask_orders", target_orders_to_string(ask_target_orders_));
            });

            auto it = ask_target_orders_.find(price);
            if(it == ask_target_orders_.end()) {
                LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "is_in_target_orders") + " " +
                                   f("result", "fail") + " " + f("reason", "price_not_found"));
                return false;
            }
            const bool result = std::abs(it->second.size - size) < config_.quantity_tick_size;
            LOG_STRATEGY_DEBUG([&]() {
                return "[TargetOrderManager] " + f("action", "is_in_target_orders") + " " +
                       f("result", result ? "pass" : "fail") + " " + f("target_size", it->second.size) + " " +
                       f("size_diff", std::abs(it->second.size - size)) + " " +
                       f("tick_size", config_.quantity_tick_size) + " " +
                       f("reason", result ? "size_diff < tick_size" : "size_diff >= tick_size");
            });
            return result;
        } else {
            LOG_STRATEGY_DEBUG([&]() {
                return "[TargetOrderManager] " + f("action", "is_in_target_orders") + " " +
                       f("all_bid_orders", target_orders_to_string(bid_target_orders_));
            });

            auto it = bid_target_orders_.find(price);
            if(it == bid_target_orders_.end()) {
                LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "is_in_target_orders") + " " +
                                   f("result", "fail") + " " + f("reason", "price_not_found"));
                return false;
            }
            const bool result = std::abs(it->second.size - size) < config_.quantity_tick_size;
            LOG_STRATEGY_DEBUG([&]() {
                return "[TargetOrderManager] " + f("action", "is_in_target_orders") + " " +
                       f("result", result ? "pass" : "fail") + " " + f("target_size", it->second.size) + " " +
                       f("size_diff", std::abs(it->second.size - size)) + " " +
                       f("tick_size", config_.quantity_tick_size) + " " +
                       f("reason", result ? "size_diff < tick_size" : "size_diff >= tick_size");
            });
            return result;
        }
    }

    [[nodiscard]] bool is_dirty(Side::Type side) const {
        return side == Side::Type::Ask ? is_ask_dirty_ : is_bid_dirty_;
    }

    [[nodiscard]] bool is_clean(Side::Type side) const { return !is_dirty(side); }

    void set_dirty(Side::Type side) {
        if(side == Side::Type::Ask) {
            is_ask_dirty_ = true;
        } else {
            is_bid_dirty_ = true;
        }
    }

    void set_clean(Side::Type side) {
        if(side == Side::Type::Ask) {
            is_ask_dirty_ = false;
        } else {
            is_bid_dirty_ = false;
        }
    }

    [[nodiscard]] bool is_in_target_orders(Side::Type side, Price price, Size size) const {
        LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "is_in_target_orders") + " " +
                           f("side", side == Side::Type::Ask ? "Ask" : "Bid") + " " + f("price", price) + " " +
                           f("size", size));

        if(side == Side::Type::Ask) {
            LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "is_in_target_orders") + " " +
                               f("all_ask_orders", target_orders_to_string(ask_target_orders_)));

            auto it = ask_target_orders_.find(price);
            if(it == ask_target_orders_.end()) {
                LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "is_in_target_orders") + " " +
                                   f("result", "fail") + " " + f("reason", "price_not_found"));
                return false;
            }
            const bool result = std::abs(it->second.size - size) < config_.quantity_tick_size;
            LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "is_in_target_orders") + " " +
                               f("result", result ? "pass" : "fail") + " " + f("target_size", it->second.size) + " " +
                               f("size_diff", std::abs(it->second.size - size)) + " " +
                               f("tick_size", config_.quantity_tick_size) + " " +
                               f("reason", result ? "size_diff < tick_size" : "size_diff >= tick_size"));
            return result;
        } else {
            LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "is_in_target_orders") + " " +
                               f("all_bid_orders", target_orders_to_string(bid_target_orders_)));

            auto it = bid_target_orders_.find(price);
            if(it == bid_target_orders_.end()) {
                LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "is_in_target_orders") + " " +
                                   f("result", "fail") + " " + f("reason", "price_not_found"));
                return false;
            }
            const bool result = std::abs(it->second.size - size) < config_.quantity_tick_size;
            LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "is_in_target_orders") + " " +
                               f("result", result ? "pass" : "fail") + " " + f("target_size", it->second.size) + " " +
                               f("size_diff", std::abs(it->second.size - size)) + " " +
                               f("tick_size", config_.quantity_tick_size) + " " +
                               f("reason", result ? "size_diff < tick_size" : "size_diff >= tick_size"));
            return result;
        }
    }

    template<Side::Type SideType>
    void refresh_target_orders() {
        LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "refresh_target_orders") + " " +
                           f("side", SideType == Side::Type::Ask ? "Ask" : "Bid") + " " + f("state", "start"));

        if(is_clean<SideType>()) {
            LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "refresh_target_orders") + " " +
                               f("result", "skip_clean"));
            return;
        }

        const double ref_mid = reference_book_.getMid();
        const double quote_mid = quote_mid_service_.shift(ref_mid);

        LOG_STRATEGY_DEBUG([&]() {
            const double ref_ask = reference_book_.getBestAsk();
            const double ref_bid = reference_book_.getBestBid();
            const double shifted_ask = quote_mid_service_.shift(ref_ask);
            const double shifted_bid = quote_mid_service_.shift(ref_bid);
            const double bid_ask_spread_bps = (ref_ask - ref_bid) / ref_mid * 10000.;
            const double half_bid_ask_spread_bps = bid_ask_spread_bps / 2.0;

            return "[TargetOrderManager] " + f("action", "calculate_mid_prices") + " " + f("ref_mid", ref_mid) + " " +
                   f("ref_ask", ref_ask) + " " + f("ref_bid", ref_bid) + " " +
                   f("bid_ask_spread_bps", bid_ask_spread_bps) + " " +
                   f("half_bid_ask_spread_bps", half_bid_ask_spread_bps) + " " + f("quote_mid", quote_mid) + " " +
                   f("shifted_ask", shifted_ask) + " " + f("shifted_bid", shifted_bid);
        }());

        const double local_ask = quote_book_.getBestAsk();
        const double local_bid = quote_book_.getBestBid();

        LOG_STRATEGY_DEBUG([&]() {
            const double local_mid = (local_ask + local_bid) / 2.0;
            return "[TargetOrderManager] " + f("action", "get_local_prices") + " " + f("local_ask", local_ask) + " " +
                   f("local_bid", local_bid) + " " + f("local_mid", local_mid);
        }());

        constexpr Side SIDE(SideType);

        // Calculate initial prices
        LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "calculate_initial_prices") + " " +
                           f("order_count", order_configs_.size()));

        for(size_t i = 0; i < order_configs_.size(); ++i) {
            const double offset = order_configs_[i].price_offset;

            const double touch_price = [&]() {
                if constexpr(SideType == Side::Type::Ask) {
                    return reference_book_.getBestAsk();
                } else {
                    return reference_book_.getBestBid();
                }
            }();

            const double base_price = config_.offset_base == OffsetBase::Mid ? quote_mid : touch_price;
            const double raw_price = base_price * SIDE.add_away(1., offset);
            prices_[i] = price_rounder_.round_price<SideType>(raw_price);

            LOG_STRATEGY_DEBUG([&]() {
                return "[TargetOrderManager] " + f("action", "calculate_price") + " " + f("index", i) + " " +
                       f("base", base_price) + " " + f("offset", offset) + " " + f("raw_price", raw_price) + " " +
                       f("rounded_price", prices_[i]);
            }());
        }

        // Apply touch price shifting if enabled
        LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "touch_price_shift_check") + " " +
                           f("enable_touch_price", config_.enable_touch_price));

        if(config_.enable_touch_price) {
            const double market_price = (SideType == Side::Type::Ask) ? local_ask : local_bid;

            LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "touch_price_shift") + " " +
                               f("market_price", market_price) + " " + f("ticks_from_touch", config_.ticks_from_touch));

            LOG_STRATEGY_DEBUG([&]() {
                std::string prices_before;
                for(const auto& p : prices_) {
                    prices_before += std::to_string(p) + ",";
                }
                return "[TargetOrderManager] " + f("action", "touch_price_shift_start") + " " +
                       f("prices", prices_before);
            }());

            touch_price_shifter_.shift<SideType>(prices_, market_price);

            LOG_STRATEGY_DEBUG([&]() {
                std::string prices_after;
                for(const auto& p : prices_) {
                    prices_after += std::to_string(p) + ",";
                }
                return "[TargetOrderManager] " + f("action", "touch_price_shift_result") + " " +
                       f("prices", prices_after);
            }());
        } else {
            LOG_STRATEGY_DEBUG([&]() {
                std::string current_prices;
                for(const auto& p : prices_) {
                    current_prices += std::to_string(p) + ",";
                }
                return "[TargetOrderManager] " + f("action", "touch_price_shift_skip") + " " + f("reason", "disabled") +
                       " " + f("current_prices", current_prices);
            }());
        }

        // Apply postable price shifting if enabled
        LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "postable_price_shift_check") + " " +
                           f("enable_postable_price", config_.enable_postable_price));

        if(config_.enable_postable_price) {
            const double market_opposite_price = (SideType == Side::Type::Ask) ? local_bid : local_ask;

            LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "postable_price_shift") + " " +
                               f("market_opposite_price", market_opposite_price) + " " +
                               f("ticks_from_postable", config_.ticks_from_postable));

            LOG_STRATEGY_DEBUG([&]() {
                std::string prices_before;
                for(const auto& p : prices_) {
                    prices_before += std::to_string(p) + ",";
                }
                return "[TargetOrderManager] " + f("action", "postable_price_shift_start") + " " +
                       f("prices", prices_before);
            }());

            postable_price_shifter_.shift<SideType>(prices_, market_opposite_price);

            LOG_STRATEGY_DEBUG([&]() {
                std::string prices_after;
                for(const auto& p : prices_) {
                    prices_after += std::to_string(p) + ",";
                }
                return "[TargetOrderManager] " + f("action", "postable_price_shift_result") + " " +
                       f("prices", prices_after);
            }());
        } else {
            std::string current_prices = "";
            for(const auto& p : prices_) {
                current_prices += std::to_string(p) + ",";
            }
            LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "postable_price_shift_skip") + " " +
                               f("reason", "disabled") + " " + f("current_prices", current_prices));
        }

        // Reset and emplace orders
        auto& target_orders = get_target_orders<SideType>();
        target_orders.clear();

        LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "create_target_orders") + " " +
                           f("count", prices_.size()));

        for(size_t i = 0; i < prices_.size(); ++i) {
            target_orders.emplace(prices_[i], TargetOrder{SideType, prices_[i], sizes_[i]});

            LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "create_order") + " " + f("index", i) + " " +
                               f("price", prices_[i]) + " " + f("size", sizes_[i]));
        }

        LOG_STRATEGY_DEBUG("[TargetOrderManager] " + f("action", "refresh_target_orders") + " " +
                           f("side", SideType == Side::Type::Ask ? "Ask" : "Bid") + " " + f("state", "complete") + " " +
                           f("orders", target_orders_to_string(target_orders)));
    }

    template<Side::Type SideType>
    [[nodiscard]] auto& get_target_orders() {
        if constexpr(SideType == Side::Type::Ask) {
            return ask_target_orders_;
        } else {
            return bid_target_orders_;
        }
    }

    template<Side::Type SideType>
    [[nodiscard]] const auto& get_target_orders() const {
        if constexpr(SideType == Side::Type::Ask) {
            return ask_target_orders_;
        } else {
            return bid_target_orders_;
        }
    }

    [[nodiscard]] size_t get_config_target_count() const { return order_configs_.size(); }

private:
    static std::vector<Size> make_sizes(const std::vector<OrderConfig>& configs, const SizeRounder& rounder) {
        std::vector<Size> sizes;
        sizes.reserve(configs.size());
        for(const auto& config : configs) {
            sizes.push_back(rounder.round(config.size));
        }
        return sizes;
    }

    const Book& quote_book_;
    const Book& reference_book_;
    const Config config_;
    const std::vector<OrderConfig> order_configs_;
    const QuoteMidServiceType& quote_mid_service_;

    const PriceRounder price_rounder_;
    const SizeRounder size_rounder_;
    const TouchPriceShifter touch_price_shifter_;
    const PostablePriceShifter postable_price_shifter_;
    std::vector<double> prices_;
    const std::vector<double> sizes_;

    AskTargetOrders ask_target_orders_;
    BidTargetOrders bid_target_orders_;

    bool is_ask_dirty_{true};
    bool is_bid_dirty_{true};
};

template<typename Orders>
[[nodiscard]]
std::string target_orders_to_string(const Orders& orders) {
    const auto order_to_string = [](const auto& order) {
        std::ostringstream oss;
        oss << "{side:" << (order.side == Side::Type::Bid ? "Buy" : "Sell") << ",price:" << std::to_string(order.price)
            << ",quantity:" << std::to_string(order.size) << "}";
        return oss.str();
    };

    std::ostringstream oss;
    oss << "{";

    bool first_price = true;
    for(const auto& [price, order] : orders) {
        if(!first_price) {
            oss << ", ";
        }
        first_price = false;

        oss << std::to_string(price) << ":" << order_to_string(order);
    }

    oss << "}";
    return oss.str();
}
