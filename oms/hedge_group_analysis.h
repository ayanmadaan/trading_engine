#pragma once

#include "../oms/bybitordermanager.hpp"
#include "../oms/okxordermanager.hpp"
#include "format.h"
#include "logging.h"
#include "type.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>

struct OrderTrace {
    using ClientOrderId = uint64_t;
    using ExchangeOrderId = uint64_t;
    using TimePoint = uint64_t;
    using Json = nlohmann::json;
    using Side = bool;
    using Quantity = double;

    // Ids
    ClientOrderId client_order_id;
    ExchangeOrderId exchange_order_id;
    Side side;
    Quantity quantity;
    VenueRole venue_role;
    // Send
    TimePoint send_time_oms;
    TimePoint live_time_exchange;
    // Cancel
    TimePoint cancel_time_oms;
    // Modification
    TimePoint modify_time_oms;

    [[nodiscard]]
    Json to_json() const {
        Json j;
        j["client_order_id"] = client_order_id;
        j["exchange_order_id"] = exchange_order_id;
        j["side"] = side ? "buy" : "sell";
        j["quantity"] = quantity;
        j["venue_role"] = venue_role_to_string(venue_role);
        auto& events = j["events"] = Json::object();
        events["send_time_oms"] = format_ns_iso8601(send_time_oms);
        events["live_time_exchange"] = format_ns_iso8601(live_time_exchange);
        events["cancel_time_oms"] = cancel_time_oms == 0 ? "N/A" : format_ns_iso8601(cancel_time_oms);
        events["modify_time_oms"] = modify_time_oms == 0 ? "N/A" : format_ns_iso8601(modify_time_oms);
        return j;
    }
};

class OrderTraceManager {
public:
    using ClientOrderId = OrderTrace::ClientOrderId;

    explicit OrderTraceManager(ByBitOrderManager& bybit_mgr, OkxOrderManager& okx_mgr)
        : bybit_order_manager_{bybit_mgr}
        , okx_order_manager_{okx_mgr} {}

    OrderTraceManager(const OrderTraceManager&) = delete;
    OrderTraceManager& operator=(const OrderTraceManager&) = delete;

    template<Exchange E>
    void add_order_trace(ClientOrderId client_order_id) {
        if(has_order_trace(client_order_id)) {
            return;
        }

        auto* order = get_order_handler<E>(client_order_id);
        if(!order) {
            throw std::runtime_error("Order not found for client_order_id: " + std::to_string(client_order_id));
        }

        VenueRole venue_role;
        if constexpr(E == Exchange::Bybit) {
            venue_role = VenueRole::Quote;
        } else if constexpr(E == Exchange::Okx) {
            venue_role = VenueRole::Hedge;
        } else {
            throw std::runtime_error("Unhandled Exchange type");
        }

        order_traces_.emplace(order->m_clientOrderId,
                              OrderTrace{.client_order_id = order->m_clientOrderId,
                                         .exchange_order_id = order->m_exchangeOrderId,
                                         .side = order->m_side,
                                         .quantity = order->m_qtySubmitted,
                                         .venue_role = venue_role,
                                         .send_time_oms = order->m_newOrderOnOmsTS,
                                         .live_time_exchange = order->m_newOrderOnExchTS,
                                         .cancel_time_oms = order->m_cancelOrderOnOmsTS,
                                         .modify_time_oms = order->m_modifyOrderOnOmsTS});
    }

    [[nodiscard]] const OrderTrace& get_order_trace(ClientOrderId client_order_id) const {
        try {
            return order_traces_.at(client_order_id);
        } catch(const std::out_of_range&) {
            throw std::out_of_range("No order trace found for client_order_id: " + std::to_string(client_order_id));
        }
    }

    void reset() { order_traces_.clear(); }

private:
    [[nodiscard]]
    bool has_order_trace(ClientOrderId client_order_id) const {
        return order_traces_.find(client_order_id) != order_traces_.end();
    }

    template<Exchange E>
    static constexpr bool always_false() {
        return false;
    }

    template<Exchange E>
    auto* get_order_handler(ClientOrderId client_order_id) {
        if constexpr(E == Exchange::Bybit) {
            return bybit_order_manager_.getOrderHandler(client_order_id);
        } else if constexpr(E == Exchange::Okx) {
            return okx_order_manager_.getOrderHandler(client_order_id);
        } else {
            static_assert(always_false<E>(), "Unhandled Exchange type");
        }
    }

    ByBitOrderManager& bybit_order_manager_;
    OkxOrderManager& okx_order_manager_;
    std::unordered_map<ClientOrderId, OrderTrace> order_traces_;
};

struct Trade {
    using ClientOrderId = uint64_t;
    using TransactionId = std::string;
    using Price = double;
    using Quantity = double;
    using Fee = double;
    using TimePoint = uint64_t;
    using Json = nlohmann::json;

    ClientOrderId client_order_id;
    TransactionId transaction_id;
    Price price;
    Quantity quantity;
    Fee fee;
    bool side; // true for buy, false for sell
    bool is_maker;
    VenueRole venue_role;
    TimePoint exchange_fill_time;
    TimePoint infra_notified_time;
    TimePoint strategy_notified_time;

    [[nodiscard]]
    Json to_json() const {
        Json j;
        j["transaction_id"] = transaction_id;
        j["price"] = price;
        j["quantity"] = quantity;
        j["fee"] = fee;
        j["side"] = side ? "buy" : "sell";
        j["liquidity_role"] = is_maker ? "maker" : "taker";
        j["venue_role"] = venue_role_to_string(venue_role);
        j["exchange_fill_time"] = format_ns_iso8601(exchange_fill_time);
        j["infra_notified_time"] = format_ns_iso8601(infra_notified_time);
        j["strategy_notified_time"] = format_ns_iso8601(strategy_notified_time);
        return j;
    }
};

class HedgeGroupAnalysis {
public:
    using Quantity = double;
    using ClientOrderId = uint64_t;
    using TimePoint = uint64_t;
    using Json = nlohmann::json;

    explicit HedgeGroupAnalysis(Quantity min_hedge_size, ByBitOrderManager& bybit_mgr, OkxOrderManager& okx_mgr)
        : min_hedge_size_{min_hedge_size}
        , order_trace_manager_{bybit_mgr, okx_mgr} {}

    template<Exchange E>
    void add_trade(const Trade& trade) {
        if(!start_time_) {
            start_time_ = trade.exchange_fill_time;
        }

        const auto& client_order_id = trade.client_order_id;
        order_trace_manager_.add_order_trace<E>(client_order_id);
        trades_.emplace_back(trade);

        update_position<E>(trade);
        update_pnl(trade);
        update_fee(trade);

        if(is_net_zero()) {
            close_time_ = trade.exchange_fill_time;
            update_win_count();
            log();
            reset();
        }
    }

private:
    std::string generate_id() {
        return "hg_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "_" +
               std::to_string(++net_zero_count_);
    }

    void reset() {
        id_ = generate_id();
        start_time_ = std::nullopt;
        quote_quantity_ = 0.0;
        hedge_quantity_ = 0.0;
        pnl_without_fee_ = 0.0;
        make_fee_ = 0.0;
        take_fee_ = 0.0;
        order_trace_manager_.reset();
        trades_.clear();
    }

    bool is_net_zero() const { return std::abs(quote_quantity_ + hedge_quantity_) < min_hedge_size_; }

    template<Exchange E>
    void update_position(const Trade& trade) {
        if constexpr(E == Exchange::Bybit) {
            quote_quantity_ += (trade.side ? trade.quantity : -trade.quantity);
        } else if constexpr(E == Exchange::Okx) {
            quote_quantity_ += (trade.side ? trade.quantity : -trade.quantity);
        }
    }

    void update_pnl(const Trade& trade) {
        if(trade.side) {
            pnl_without_fee_ -= trade.price * trade.quantity;
        } else {
            pnl_without_fee_ += trade.price * trade.quantity;
        }
    }

    void update_fee(const Trade& trade) {
        if(trade.is_maker) {
            make_fee_ += trade.fee;
        } else {
            take_fee_ += trade.fee;
        }
    }

    double get_pnl_without_fee() const { return pnl_without_fee_; }
    double get_pnl_with_fee() const { return get_pnl_without_fee() - get_totol_fee(); }
    double get_totol_fee() const { return make_fee_ + take_fee_; }
    double get_maker_fee() const { return make_fee_; }
    double get_taker_fee() const { return take_fee_; }

    void update_win_count() {
        if(get_pnl_with_fee() > 0) {
            win_count_++;
        }
    }

    void log() const {
        if(!start_time_) {
            return;
        }

        Json json_output;

        // Basic information
        json_output["id"] = id_;
        json_output["net_zero_count"] = net_zero_count_;
        json_output["win_count"] = win_count_;
        json_output["win_rate"] = static_cast<double>(win_count_) / net_zero_count_;
        json_output["start_time"] = format_ns_iso8601(*start_time_);
        json_output["close_time"] = format_ns_iso8601(close_time_);

        // Calculate duration in microseconds
        auto diff_ns = close_time_ - *start_time_;
        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::nanoseconds(diff_ns));
        json_output["duration_us"] = duration_us.count();

        // PnL information
        auto& pnl_json = json_output["pnl"] = Json::object();
        pnl_json["pnl_without_fee"] = get_pnl_without_fee();
        pnl_json["pnl_with_fee"] = get_pnl_with_fee();
        pnl_json["maker_fee"] = get_maker_fee();
        pnl_json["taker_fee"] = get_taker_fee();
        pnl_json["total_fee"] = get_totol_fee();

        // Position information
        auto& position_json = json_output["position"] = Json::object();
        position_json["quote_quantity"] = quote_quantity_;
        position_json["hedge_quantity"] = hedge_quantity_;

        // Organize trades by order
        std::unordered_map<ClientOrderId, Json> id_to_trace;
        std::unordered_map<ClientOrderId, std::vector<Json>> id_to_trades;

        // First, collect all fills by order
        for(const auto& trade : trades_) {
            if(id_to_trace.find(trade.client_order_id) == id_to_trace.end()) {
                id_to_trace[trade.client_order_id] =
                    order_trace_manager_.get_order_trace(trade.client_order_id).to_json();
            }

            id_to_trades[trade.client_order_id].push_back(trade.to_json());
        }

        // Now build the orders section with fills
        auto& orders_json = json_output["orders"] = Json::object();
        for(const auto& [id, trace] : id_to_trace) {
            Json order_json = trace;

            // Calculate filled quantity for this order
            double filled_quantity = 0.0;
            for(const auto& trade : id_to_trades[id]) {
                filled_quantity += trade["quantity"].get<double>();
            }
            order_json["filled_quantity"] = filled_quantity;

            // Add fills
            order_json["fills"] = id_to_trades[id];

            orders_json[std::to_string(id)] = order_json;
        }
        json_output["orders"] = orders_json;

        log_action_pass("hedge_group_analysis", json_output.dump());
    }

    const Quantity min_hedge_size_;
    int net_zero_count_{0};
    int win_count_{0};
    std::string id_{generate_id()};
    std::optional<TimePoint> start_time_;
    TimePoint close_time_{};

    Quantity quote_quantity_{0.0};
    Quantity hedge_quantity_{0.0};
    double pnl_without_fee_{0.0};
    double make_fee_{0.0};
    double take_fee_{0.0};

    OrderTraceManager order_trace_manager_;
    std::vector<Trade> trades_;
};
