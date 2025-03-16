#pragma once

#include "../utils/instrumentmappings.hpp"
#include "../utils/staticparams.hpp"
#include "Configuration.h"
#include "PnlManager.h"
#include "bybitclient.hpp"
#include "format.h"
#include "okxclient.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

struct ReconciliationResult {
    bool is_matched;
    double expected_pnl;
    double actual_pnl;
    double difference;
    double position_difference;
    double average_cost_difference;
};

template<BookConcept Book>
class PnlReconciler {
public:
    using PnlReconCallback = std::function<void(bool)>;
    struct VerifiedPnlSnapshot {
        std::chrono::system_clock::time_point timestamp;
        double realized_pnl;
        double position;
        double average_cost;
        double maker_fee;
        double taker_fee;
    };

    struct Trade {
        const double quantity;
        const double price;
        const double fee;
        const bool is_maker;
    };

    explicit PnlReconciler(PnlManager<Book>& pnl_manager, const Configuration& config)
        : pnl_manager_(pnl_manager)
        , last_verified_snapshot_()
        , trades_since_verification_()
        , normal_recon_interval(config.child("pnl_recon").get<uint32_t>("normal_recon_interval_ms", 5000))
        , failure_recon_interval(config.child("pnl_recon").get<uint32_t>("failure_recon_interval_ms", 3000))
        , max_failure_query_cnt(config.child("pnl_recon").get<uint32_t>("max_failure_query_cnt", 3))
        , m_bybitClient(
              config.child("trading_control").get<bool>("live_trading_enabled", false),
              config.child("markets").child("quote").child("exchange_keys").get<std::string>("api_key", ""),
              config.child("markets").child("quote").child("exchange_keys").get<std::string>("api_secret", ""))
        , m_okxClient(
              config.child("trading_control").get<bool>("live_trading_enabled", false),
              config.child("markets").child("hedge").child("exchange_keys").get<std::string>("api_key", ""),
              config.child("markets").child("hedge").child("exchange_keys").get<std::string>("api_secret", ""),
              config.child("markets").child("hedge").child("exchange_keys").get<std::string>("api_passphrase", "")) {
        std::string quote_instrument = config.child("markets").child("quote").get<std::string>("name", "");
        bybitInfo = mapping::getInstrumentInfo(quote_instrument);
        std::string hedge_instrument = config.child("markets").child("hedge").get<std::string>("name", "");
        okxInfo = mapping::getInstrumentInfo(hedge_instrument);
        set_verified_snapshot(std::chrono::system_clock::now(),
                              pnl_manager_.get_realized_pnl(),
                              pnl_manager_.get_position(),
                              pnl_manager_.get_average_cost(),
                              pnl_manager_.get_maker_fee(),
                              pnl_manager_.get_taker_fee());
    }

    ~PnlReconciler() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_running) {
            stop();
        }
    }

    void start(PnlReconCallback reconCallback = nullptr) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_running) {
            return;
        }
        if(m_reconThread.joinable()) {
            m_reconThread.join();
        }
        m_reconCallback = reconCallback;
        m_running = true;
        m_nextReconTime = std::chrono::system_clock::now();
        m_reconThread = std::thread(&PnlReconciler::reconciliationLoop, this);
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if(!m_running) {
                return; // Already stopped
            }
            m_running = false;
        }
        m_reconCV.notify_all(); // Wake up ALL waiting threads

        if(m_reconThread.joinable()) {
            m_reconThread.join();
        }
    }

    void reconciliationLoop() {
        while(m_running) {
            try {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_reconCV.wait_until(lock, m_nextReconTime, [this]() {
                    return !m_running || std::chrono::system_clock::now() >= m_nextReconTime;
                });
                if(!m_running) break;
                lock.unlock();
                auto res = reconcile();
                m_reconCallback(res);
            } catch(const std::exception& e) {
                LoggerSingleton::get().infra().error("error in reconciliation loop: ", e.what());
                std::lock_guard<std::mutex> lock(m_mutex);
                m_nextReconTime = std::chrono::system_clock::now() + std::chrono::milliseconds(failure_recon_interval);
            }
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
    }

    void set_verified_snapshot(std::chrono::system_clock::time_point timestamp,
                               double realized_pnl,
                               double position,
                               double average_cost,
                               double maker_fee,
                               double taker_fee) {
        last_verified_snapshot_ =
            VerifiedPnlSnapshot{timestamp, realized_pnl, position, average_cost, maker_fee, taker_fee};
        trades_since_verification_.clear();
    }

    void add_trade(double quantity, double price, double fee, bool is_maker) {
        trades_since_verification_.push_back(Trade{quantity, price, fee, is_maker});
    }

    [[nodiscard]]
    bool reconcile() {
        // Calculate expected values using temporary manager
        std::chrono::system_clock::time_point reconTime = m_nextReconTime;
        if(last_verified_snapshot_) {
            auto start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                last_verified_snapshot_->timestamp.time_since_epoch())
                                .count();
            auto end_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>((m_nextReconTime).time_since_epoch()).count();
            if(start_ms >= end_ms) {
                LoggerSingleton::get().infra().warning(
                    "start time is greater or equal to end time for fetching trade history");
                return false;
            }
            auto response = m_bybitClient.getTradeHistory(bybitInfo.category, bybitInfo.instrument, start_ms, end_ms);
            auto result = m_okxClient.getTradeHistory(okxInfo.category, okxInfo.instrument, start_ms, end_ms);
            if(response.first && result.first) {
                m_reconRetryCounter = 0;
                m_nextReconTime += std::chrono::milliseconds(normal_recon_interval);
                parseByBitResponse(response.second);
                parseOkxResponse(result.second);
            } else {
                m_nextReconTime += std::chrono::milliseconds(failure_recon_interval);
                m_reconRetryCounter += 1;
                if(m_reconRetryCounter >= max_failure_query_cnt) {
                    return false;
                }
                return true;
            }
        }

        PnlManager<Book> temp_manager(pnl_manager_.get_hedge_book());

        if(last_verified_snapshot_) {
            temp_manager.adjust_state(last_verified_snapshot_->position,
                                      last_verified_snapshot_->average_cost,
                                      last_verified_snapshot_->realized_pnl,
                                      last_verified_snapshot_->maker_fee,
                                      last_verified_snapshot_->taker_fee);
        }

        // Replay all new trades
        for(const auto& trade : trades_since_verification_) {
            temp_manager.add_trade(trade.quantity, trade.price, trade.fee, trade.is_maker);
        }

        const double expected_position = temp_manager.get_position();
        const double expected_average_cost = temp_manager.get_average_cost();
        const double expected_realized_pnl = temp_manager.get_realized_pnl();
        const double expected_pnl = temp_manager.get_total_pnl();
        const double expected_maker_fee = temp_manager.get_maker_fee();
        const double expected_taker_fee = temp_manager.get_taker_fee();

        // Get current values
        const double actual_pnl = pnl_manager_.get_total_pnl();
        const double actual_position = pnl_manager_.get_position();
        const double actual_average_cost = pnl_manager_.get_average_cost();
        const double actual_maker_fee = pnl_manager_.get_maker_fee();
        const double actual_taker_fee = pnl_manager_.get_taker_fee();

        // Calculate differences
        const double pnl_difference = actual_pnl - expected_pnl;
        const double position_difference = actual_position - expected_position;
        const double average_cost_difference = actual_average_cost - expected_average_cost;
        const double maker_fee_difference = actual_maker_fee - expected_maker_fee;
        const double taker_fee_difference = actual_taker_fee - expected_taker_fee;

        const bool is_matched = (pnl_difference == 0. && position_difference == 0. && average_cost_difference == 0. &&
                                 maker_fee_difference == 0. && taker_fee_difference == 0.);

        if(!is_matched) {
            // Update pnl_manager state to match temp_manager if there's a mismatch
            LoggerSingleton::get().infra().warning(f("action", "pnl_reconciliation_needs_to_be_done"));
            pnl_manager_.adjust_state(temp_manager.get_position(),
                                      temp_manager.get_average_cost(),
                                      temp_manager.get_realized_pnl(),
                                      temp_manager.get_maker_fee(),
                                      temp_manager.get_taker_fee());
            LoggerSingleton::get().infra().info(
                f("action", "pnl_reconciliation") + " " + f("result", "adjusted") + " " +
                f("old_position", std::to_string(actual_position)) + " " +
                f("new_position", std::to_string(temp_manager.get_position())) + " " +
                f("old_average_cost", std::to_string(actual_average_cost)) + " " +
                f("new_average_cost", std::to_string(temp_manager.get_average_cost())) + " " +
                f("old_realized_pnl", std::to_string(pnl_manager_.get_realized_pnl())) + " " +
                f("new_realized_pnl", std::to_string(temp_manager.get_realized_pnl())) + " " +
                f("old_total_pnl", std::to_string(actual_pnl)) + " " +
                f("new_total_pnl", std::to_string(expected_pnl)) + " " +
                f("old_maker_fee", std::to_string(actual_maker_fee)) + " " +
                f("new_maker_fee", std::to_string(expected_maker_fee)) + " " +
                f("old_taker_fee", std::to_string(actual_taker_fee)) + " " +
                f("new_taker_fee", std::to_string(expected_taker_fee)));
        }

        // Update snapshot with realized PnL
        last_verified_snapshot_ = VerifiedPnlSnapshot{reconTime,
                                                      pnl_manager_.get_realized_pnl(),
                                                      pnl_manager_.get_position(),
                                                      pnl_manager_.get_average_cost(),
                                                      pnl_manager_.get_maker_fee(),
                                                      pnl_manager_.get_taker_fee()};

        trades_since_verification_.clear();

        return true;
    }

    [[nodiscard]] const std::optional<VerifiedPnlSnapshot>& get_last_verified_snapshot() const {
        return last_verified_snapshot_;
    }

    [[nodiscard]] const std::vector<Trade>& get_trades_since_verification() const { return trades_since_verification_; }

private:
    void parseByBitResponse(std::string& jsonResponse) {
        try {
            json response = json::parse(jsonResponse);
            if(response["retCode"] != 0) {
                LoggerSingleton::get().infra().error("error: ", response["retMsg"]);
                return;
            }

            for(const auto& tradeJson : response["result"]["list"]) {
                /*Useful if Trade is extended in future!!*/
                // trade.symbol = tradeJson["symbol"];
                // trade.orderType = tradeJson["orderType"];
                // trade.orderId = tradeJson["orderId"];
                // trade.execTime = tradeJson["execTime"];
                // trade.execPrice = std::stod(tradeJson["execPrice"].get<std::string>());
                // trade.execValue = std::stod(tradeJson["execValue"].get<std::string>());
                // trade.execQty = std::stod(tradeJson["execQty"].get<std::string>());
                // trade.isMaker = tradeJson["isMaker"];
                /*
                {"retCode":0,"retMsg":"OK","result":{"nextPageCursor":"1741075200000%3A17889272%2C1741075200000%3A17889272",
                "category":"linear","list":[{"symbol":"BTCUSDT","orderType":"UNKNOWN","underlyingPrice":"","orderLinkId":"",
                "orderId":"c0355078-46fe-4062-aecd-64119ff71555","stopOrderType":"UNKNOWN","execTime":"1741075200000",
                "feeCurrency":"","createType":"","feeRate":"0.000002","tradeIv":"","blockTradeId":"","markPrice":"83135.2",
                "execPrice":"83138.7","markIv":"","orderQty":"0","orderPrice":"0","execValue":"665.1096","closedSize":"0",
                "execType":"Funding","seq":342003405057,"side":"Buy","indexPrice":"","leavesQty":"0","isMaker":false,"execFee":"0.00196873",
                "execId":"842bf865-c3fc-42ce-9995-feffd2a9b351","marketUnit":"","execQty":"0.008"}]},
                "retExtInfo":{},"time":1741075208748}
                */

                const std::string execType = tradeJson["execType"];
                if (execType == "Funding") {
                    continue;
                }
                const double quantity = [&tradeJson] {
                    double quantity = std::stod(tradeJson["execQty"].get<std::string>());

                    // Apply sell side adjustment
                    if(tradeJson["side"] == "Sell") {
                        quantity *= -1;
                    }

                    return quantity;
                }();

                const double price = std::stod(tradeJson["execPrice"].get<std::string>());

                const bool is_maker = tradeJson["isMaker"].get<bool>();

                const double fee = std::stod(tradeJson["execFee"].get<std::string>());

                trades_since_verification_.emplace_back(quantity, price, fee, is_maker);
            }
        } catch(const std::exception& e) {
            LoggerSingleton::get().infra().error("parsing error in pnl recon: ", e.what());
        }
    }

    void parseOkxResponse(std::string& jsonResponse) {
        try {
            json response = json::parse(jsonResponse);
            if(response["code"] != "0") {
                LoggerSingleton::get().infra().error("error: ", response["msg"]);
                return;
            }
            for(const auto& tradeJson : response["data"]) {
                // trade.fillPnl = std::stod(tradeJson["fillPnl"].get<std::string>());
                // trade.orderId = tradeJson["ordId"];
                // trade.instType = tradeJson["instType"];
                // trade.instId = tradeJson["instId"];
                // trade.posSide = tradeJson["posSide"];
                // trade.tradeId = tradeJson["tradeId"];
                // trade.feeCurrency = tradeJson["feeCcy"];
                // trade.fee = std::stod(tradeJson["fee"].get<std::string>());
                // trade.fillTime = std::stoull(tradeJson["fillTime"].get<std::string>());

                const double quantity = [&tradeJson] {
                    double quantity = std::stod(tradeJson["fillSz"].get<std::string>());

                    // Apply sell side adjustment
                    if(tradeJson["side"] == "sell") {
                        quantity *= -1;
                    }

                    // Apply instrument-specific multipliers
                    const std::string instId = tradeJson["instId"];
                    if(instId == "DOGE-USDT-SWAP") {
                        quantity *= okx::DOGE_USDT_SWAP::dogePerpCtMul * okx::DOGE_USDT_SWAP::dogePerpCtVal;
                    } else if(instId == "BTC-USDT-SWAP") {
                        quantity *= okx::BTC_USDT_SWAP::btcPerpCtMul * okx::BTC_USDT_SWAP::btcPerpCtVal;
                    }

                    return quantity;
                }();

                const double price = std::stod(tradeJson["fillPx"].get<std::string>());
                const bool is_maker = (tradeJson["execType"] != "T");

                const double fee = std::stod(tradeJson["fee"].get<std::string>()) * -1;

                trades_since_verification_.emplace_back(quantity, price, fee, is_maker);
            }
        } catch(const std::exception& e) {
            LoggerSingleton::get().infra().error("error in parsing okx pnl recon response", e.what());
        }
    }
    PnlManager<Book>& pnl_manager_;
    std::optional<VerifiedPnlSnapshot> last_verified_snapshot_;
    std::vector<Trade> trades_since_verification_;
    std::thread m_reconThread;
    uint32_t normal_recon_interval;
    uint32_t failure_recon_interval;
    uint32_t max_failure_query_cnt;
    std::atomic<bool> m_running{false};
    std::atomic<uint32_t> m_reconRetryCounter{0};
    std::condition_variable m_reconCV;
    mutable std::mutex m_mutex;
    BybitClient m_bybitClient;
    OkxClient m_okxClient;
    std::chrono::system_clock::time_point m_nextReconTime{std::chrono::system_clock::now()};
    mapping::InstrumentInfo bybitInfo;
    mapping::InstrumentInfo okxInfo;
    PnlReconCallback m_reconCallback;
};
