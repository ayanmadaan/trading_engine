#include "../infra/binancewebsocket.hpp"
#include "../infra/bybitwebsocket.hpp"
#include "../infra/okxwebsocket.hpp"
#include "../infra/timer.hpp"
#include "../oms/bybitfills.hpp"
#include "../oms/bybitordermanager.hpp"
#include "../oms/bybitpositionmanager.hpp"
#include "../oms/okxordermanager.hpp"
#include "../oms/okxpositionmanager.hpp"
#include "../src/ExposureMonitor.h"
#include "../src/TradeAnalysis.h"
#include "../utils/connections.hpp"
#include "../utils/helper.hpp"
#include "../utils/instrumentmappings.hpp"
#include "../utils/logger.hpp"
#include "../utils/pinthreads.hpp"
#include "Configuration.h"
#include "ExchangePnlService.h"
#include "Hedger.h"
#include "OrderHealthCheck.h"
#include "PendingCancellationManager.h"
#include "PendingModificationManager.h"
#include "PendingSubmissionManager.h"
#include "PnlManager.h"
#include <memory>
#include <sstream>
#include <string>
#include <thread>

class Strategy {
public:
    explicit Strategy(Configuration config)
        : config_(std::move(config)) {
        log_action_pass("construct_strategy");
        start_all_ws();
        start_timer();
    }

    // Delete copy constructor and assignment
    Strategy(const Strategy&) = delete;
    Strategy& operator=(const Strategy&) = delete;

    // Delete move constructor and assignment
    Strategy(Strategy&&) = delete;
    Strategy& operator=(Strategy&&) = delete;

    ~Strategy() {
        cleanup();
        log_action_pass("destruct_strategy");
    }

    /* -------------------------------------------------------------------------- */
    /*                           Trading Initialization                           */
    /* -------------------------------------------------------------------------- */

    // NOTE: This function is called by class Signal at infra side
    void initialize_trading() {
        setup_thread_affinity();
        setup_callbacks();
        log_action_pass("initialize_trading");
    }

    void setup_thread_affinity() {
        constexpr int binance_core = 0;
        constexpr int bybit_core = 1;
        constexpr int okx_core = 2;
        constexpr int okx_order_core = 3;
        constexpr int bybit_order_core = 4;
        constexpr int bybit_fills_core = bybit_order_core;
        constexpr int bybit_position_core = 5;
        constexpr int okx_position_core = 6;

        setThreadAffinity(threads_.binance, binance_core);
        setThreadAffinity(threads_.bybit, bybit_core);
        setThreadAffinity(threads_.okx, okx_core);
        setThreadAffinity(threads_.okx_order_manager, okx_order_core);
        setThreadAffinity(threads_.bybit_order_manager, bybit_order_core);
        setThreadAffinity(threads_.bybit_fills, bybit_fills_core);
        bybit_position_manager_.pinThread(bybit_position_core);
        okx_position_manager_.pinThread(okx_position_core);

        log_action_pass("setup_thread_affinity",
                        f("bybit_core", bybit_core),
                        f("bybit_order_core", bybit_order_core),
                        f("bybit_position_core", bybit_position_core),
                        f("bybit_fills_core", bybit_fills_core),
                        f("okx_core", okx_core),
                        f("okx_order_core", okx_order_core),
                        f("okx_position_core", okx_position_core),
                        f("binance_core", binance_core));
    }


    // NOTE: This function is called by class Signal at infra side
    bool is_trading_ready() const {
        if(!binance_ws_.isBookReady()) {
            log_action_fail<LogLevel::WARNING>("check_trading_ready", "binance_ws_not_ready");
            return false;
        } else if(!bybit_ws_.isBookReady()) {
            log_action_fail<LogLevel::WARNING>("check_trading_ready", "bybit_ws_not_ready");
            return false;
        } else if(!okx_ws_.isBookReady()) {
            log_action_fail<LogLevel::WARNING>("check_trading_ready", "okx_ws_not_ready");
            return false;
        } else if(!bybit_position_manager_.isPosReconWarmedUp()) {
            log_action_fail<LogLevel::WARNING>("check_trading_ready", "bybit_position_manager_not_ready");
            return false;
        } else if(!okx_position_manager_.isPosReconWarmedUp()) {
            log_action_fail<LogLevel::WARNING>("check_trading_ready", "okx_position_manager_not_ready");
            return false;
        }
        log_action_pass("check_trading_ready");
        return true;
    }

    // NOTE: This function is called by class Signal at infra side
    void start_trading() {
        event_processor_.start();
        event_processor_.submit({EventType::StartTrading, {}});
    }

private:
    /* -------------------------------------------------------------------------- */
    /*                          Internal Data Structures                          */
    /* -------------------------------------------------------------------------- */

    struct Threads {
        std::thread okx;
        std::thread binance;
        std::thread bybit;
        std::thread okx_order_manager;
        std::thread strategy;
        std::thread bybit_order_manager;
        std::thread bybit_fills;
    };

    /* -------------------------------------------------------------------------- */
    /*                          Event Processing Logic                            */
    /* -------------------------------------------------------------------------- */

    enum class EventType : std::uint8_t {
        // Trading Control
        StartTrading,
        StopTrading,
        // Market Updates
        BybitMarketUpdate,
        OkxMarketUpdate,
        BinanceMarketUpdate,
        // Order Updates
        BybitOrderUpdate,
        OkxOrderUpdate,
        // Recon
        PositionRecon,
        PnlRecon,
        // WebSocket Disconnected
        WebSocketDisconnected,
    };

    // Trading Control
    struct StartTradingEventData {};
    struct StopTradingEventData {
        std::string reason;
    };
    // Market Data
    struct MarketUpdateEventData {};
    // Order Updates
    struct OrderUpdateEventData {
        uint64_t m_newOrderOnOmsTS = 0;
        uint64_t m_newOrderOnExchTS = 0;
        uint64_t m_newOrderConfirmationTS = 0;
        uint64_t m_modifyOrderOnOmsTS = 0;
        uint64_t m_modifyOrderOnExchTS = 0;
        uint64_t m_modifyOrderConfirmationTS = 0;
        uint64_t m_cancelOrderOnOmsTS = 0;
        uint64_t m_cancelOrderOnExchTS = 0;
        uint64_t m_cancelOrderConfirmationTS = 0;
        uint64_t m_rejectionTS = 0;
        uint64_t m_executedTS = 0;
        uint64_t m_executedTSOnOms = 0;

        bool m_side = false;
        bool m_orderHasBeenLive = false;
        uint64_t m_exchangeOrderId = 0;
        uint64_t m_clientOrderId = 0;

        double m_cumFilledQty = 0.0;
        double m_cumFee = 0.0;
        double m_fillFee = 0.0;
        double m_fillPx = 0.0;
        double m_fillSz = 0.0;
        double m_fillPnl = 0.0;
        bool m_fillMaker = false;
        std::string m_transactionId;

        double m_priceOnExch = 0;
        double m_qtyOnExch = 0;
        double m_qtySubmitted = 0;
        double m_priceSubmitted = 0;

        uint64_t m_placeOrderNow = 0;
        std::string m_instrumentId = "";

        OrderStatus m_status;
        std::string m_statusStr;
        RejectReason m_reason;
        std::string m_reasonStr;
    };

    // Recon
    struct PositionReconEventData {
        ReconStatus status;
    };
    struct PnlReconEventData {
        bool status;
    };
    // WebSocket Disconnected
    struct WsDisconnectedEventData {
        bool reached_retry_limit;
    };

    using EventData = std::variant<
        // Trading Control
        StartTradingEventData,
        StopTradingEventData,
        // Market Data
        MarketUpdateEventData,
        // Order Updates
        OrderUpdateEventData,
        // Recon
        PositionReconEventData,
        PnlReconEventData,
        // WebSocket Disconnected
        WsDisconnectedEventData>;

    struct Event {
        EventType type;
        EventData data;
    };

    class EventQueue {
    private:
        std::queue<Event> queue_;
        std::mutex mutex_;
        std::condition_variable condition_;
        std::atomic<bool> running_{true};

    public:
        // Enqueue operation - called by each worker thread
        void push(Event event) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                queue_.push(std::move(event));
            }
            condition_.notify_one();
        }

        // Dequeue operation - called by the processing thread
        bool pop(Event& event) {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return !queue_.empty() || !running_; });

            if(!running_ && queue_.empty()) {
                return false;
            }

            event = std::move(queue_.front());
            queue_.pop();
            return true;
        }

        // Stop queue processing
        void stop() {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                running_ = false;
            }
            condition_.notify_all();
        }

        // Check if the queue is running
        bool is_running() const { return running_; }
    };

    class EventProcessor {
    public:
        EventProcessor(){}

    private:
        // TradeAnalyzers
        EventQueue event_queue_;
        std::thread processor_thread_;

    public:
        // Start processing thread
        void start() { processor_thread_ = std::thread(&EventProcessor::process_events, this); }

        // Stop processing thread
        void stop() {
            event_queue_.stop();
            if(processor_thread_.joinable()) {
                processor_thread_.join();
            }
        }

        // Submit event interface - called by each worker thread
        void submit(Event event) { event_queue_.push(std::move(event)); }

    private:
        void process_events() {
            while(event_queue_.is_running()) {
                Event event;
                if(event_queue_.pop(event)) {
                    handle_event(event);
                }
            }
        }

        void handle_event(const Event& event) {
            switch(event.type) {
            // Trading Control
            case EventType::StartTrading: {
                Strategy::handle_start_trading();
                break;
            }
            case EventType::StopTrading: {
                Strategy::handle_stop_trading(std::get<StopTradingEventData>(event.data));
                break;
            }
            // Market Updates
            case EventType::BybitMarketUpdate: {
                Strategy::handle_bybit_market_update();
                break;
            }
            case EventType::BinanceMarketUpdate: {
                Strategy::handle_binance_market_update();
                break;
            }
            case EventType::OkxMarketUpdate: {
                Strategy::handle_okx_market_update();
                break;
            }
            // Order Updates
            case EventType::BybitOrderUpdate: {
                Strategy::handle_bybit_order_update(std::get<OrderUpdateEventData>(event.data));
                break;
            }
            case EventType::OkxOrderUpdate: {
                Strategy::handle_okx_order_update(std::get<OrderUpdateEventData>(event.data));
                break;
            }
            // Recon
            case EventType::PositionRecon: {
                Strategy::handle_position_recon(std::get<PositionReconEventData>(event.data));
                break;
            }
            // WebSocket Disconnected
            case EventType::WebSocketDisconnected: {
                Strategy::handle_ws_disconnected(std::get<WsDisconnectedEventData>(event.data));
                break;
            }
            }
        }

        /* -------------------------------------------------------------------------- */
        /*                        Quote Order Update Callbacks                        */
        /* -------------------------------------------------------------------------- */
    };

    // Callback Adapter - used to convert existing callbacks into events
    class CallbackAdapter {
    public:
        explicit CallbackAdapter(EventProcessor& processor)
            : processor_(processor) {}

        std::function<void()> create_binance_market_update_callback() {
            return [this]() {
                MarketUpdateEventData data{};
                processor_.submit({EventType::BinanceMarketUpdate, data});
            };
        }

        std::function<void()> create_bybit_market_update_callback() {
            return [this]() {
                MarketUpdateEventData data{};
                processor_.submit({EventType::BybitMarketUpdate, data});
            };
        }

        std::function<void()> create_okx_market_update_callback() {
            return [this]() {
                MarketUpdateEventData data{};
                processor_.submit({EventType::OkxMarketUpdate, data});
            };
        }

        std::function<void(OrderHandler&)> create_bybit_order_update_callback() {
            return [this](OrderHandler& order) {
                OrderUpdateEventData data{.m_newOrderOnOmsTS = order.m_newOrderOnOmsTS,
                                          .m_newOrderOnExchTS = order.m_newOrderOnExchTS,
                                          .m_newOrderConfirmationTS = order.m_newOrderConfirmationTS,
                                          .m_modifyOrderOnOmsTS = order.m_modifyOrderOnOmsTS,
                                          .m_modifyOrderOnExchTS = order.m_modifyOrderOnExchTS,
                                          .m_modifyOrderConfirmationTS = order.m_modifyOrderConfirmationTS,
                                          .m_cancelOrderOnOmsTS = order.m_cancelOrderOnOmsTS,
                                          .m_cancelOrderOnExchTS = order.m_cancelOrderOnExchTS,
                                          .m_cancelOrderConfirmationTS = order.m_cancelOrderConfirmationTS,
                                          .m_rejectionTS = order.m_rejectionTS,
                                          .m_executedTS = order.m_executedTS,
                                          .m_executedTSOnOms = order.m_executeTSOnOms,
                                          .m_side = order.m_side,
                                          .m_orderHasBeenLive = order.m_orderHasBeenLive,
                                          .m_exchangeOrderId = order.m_exchangeOrderId,
                                          .m_clientOrderId = order.m_clientOrderId,
                                          .m_cumFilledQty = order.m_cumFilledQty,
                                          .m_cumFee = order.m_cumFee,
                                          .m_fillFee = order.m_fillFee,
                                          .m_fillPx = order.m_fillPx,
                                          .m_fillSz = order.m_fillSz,
                                          .m_fillPnl = order.m_fillPnl,
                                          .m_fillMaker = order.m_fillMaker,
                                          .m_transactionId = order.m_transactionId,
                                          .m_priceOnExch = order.m_priceOnExch,
                                          .m_qtyOnExch = order.m_qtyOnExch,
                                          .m_qtySubmitted = order.m_qtySubmitted,
                                          .m_priceSubmitted = order.m_priceSubmitted,
                                          .m_status = order.m_status,
                                          .m_statusStr = order.getCurrentStatusStr(),
                                          .m_reason = order.m_reason,
                                          .m_reasonStr = order.getRejectReasonStr()};
                processor_.submit({EventType::BybitOrderUpdate, data});
            };
        }

        std::function<void(OrderHandler&)> create_okx_order_update_callback() {
            return [this](OrderHandler& order) {
                OrderUpdateEventData data{.m_newOrderOnOmsTS = order.m_newOrderOnOmsTS,
                                          .m_newOrderOnExchTS = order.m_newOrderOnExchTS,
                                          .m_newOrderConfirmationTS = order.m_newOrderConfirmationTS,
                                          .m_modifyOrderOnOmsTS = order.m_modifyOrderOnOmsTS,
                                          .m_modifyOrderOnExchTS = order.m_modifyOrderOnExchTS,
                                          .m_modifyOrderConfirmationTS = order.m_modifyOrderConfirmationTS,
                                          .m_cancelOrderOnOmsTS = order.m_cancelOrderOnOmsTS,
                                          .m_cancelOrderOnExchTS = order.m_cancelOrderOnExchTS,
                                          .m_cancelOrderConfirmationTS = order.m_cancelOrderConfirmationTS,
                                          .m_rejectionTS = order.m_rejectionTS,
                                          .m_executedTS = order.m_executedTS,
                                          .m_executedTSOnOms = order.m_executeTSOnOms,
                                          .m_side = order.m_side,
                                          .m_orderHasBeenLive = order.m_orderHasBeenLive,
                                          .m_exchangeOrderId = order.m_exchangeOrderId,
                                          .m_clientOrderId = order.m_clientOrderId,
                                          .m_cumFilledQty = order.m_cumFilledQty,
                                          .m_cumFee = order.m_cumFee,
                                          .m_fillFee = order.m_fillFee,
                                          .m_fillPx = order.m_fillPx,
                                          .m_fillSz = order.m_fillSz,
                                          .m_fillPnl = order.m_fillPnl,
                                          .m_fillMaker = order.m_fillMaker,
                                          .m_transactionId = order.m_transactionId,
                                          .m_priceOnExch = order.m_priceOnExch,
                                          .m_qtyOnExch = order.m_qtyOnExch,
                                          .m_qtySubmitted = order.m_qtySubmitted,
                                          .m_priceSubmitted = order.m_priceSubmitted,
                                          .m_status = order.m_status,
                                          .m_statusStr = order.getCurrentStatusStr(),
                                          .m_reason = order.m_reason,
                                          .m_reasonStr = order.getRejectReasonStr()};
                processor_.submit({EventType::OkxOrderUpdate, data});
            };
        }

        std::function<void(ReconStatus)> create_position_recon_callback() {
            return [this](ReconStatus status) {
                PositionReconEventData data{.status = status};
                processor_.submit({EventType::PositionRecon, data});
            };
        }

        std::function<void(bool)> create_ws_disconnected_callback() {
            return [this](bool reached_retry_limit) {
                WsDisconnectedEventData data{.reached_retry_limit = reached_retry_limit};
                processor_.submit({EventType::WebSocketDisconnected, data});
            };
        }

    private:
        EventProcessor& processor_;
    };

    /* -------------------------------------------------------------------------- */
    /*                         STATIC CONSTRUCTION HELPERS                        */
    /* -------------------------------------------------------------------------- */


    static ByBitPositionManager create_bybit_position_manager(const Configuration& config) {
        std::string quote_instrument = config.child("markets").child("quote").get<std::string>("name");
        mapping::InstrumentInfo bybit_instrument_info = mapping::getInstrumentInfo(quote_instrument);
        return ByBitPositionManager{
            config.child("trading_control").get<bool>("live_trading_enabled"),
            config.child("bybit_position").get<double>("max_position", 0.0),
            config.child("bybit_position").get<double>("base_position", 0.0),
            config.child("markets").child("quote").child("tick_sizes").get<double>("quantity"),
            config.child("bybit_recon").get<double>("tolerable_threshold", 1.0),
            config.child("bybit_recon").get<uint32_t>("max_mismatch_cnt", 3),
            config.child("bybit_recon").get<uint32_t>("max_failure_query_cnt", 5),
            config.child("bybit_recon").get<uint32_t>("retry_interval_on_failure_ms", 2000),
            config.child("bybit_recon").get<uint32_t>("normal_recon_interval_ms", 5000),
            config.child("bybit_recon").get<uint32_t>("retry_interval_on_mismatch_ms", 3000),
            bybit_instrument_info.category,
            bybit_instrument_info.instrument,
            config.child("markets").child("quote").child("exchange_keys").get<std::string>("api_key"),
            config.child("markets").child("quote").child("exchange_keys").get<std::string>("api_secret")};
    }

    static OkxPositionManager create_okx_position_manager(const Configuration& config) {
        std::string hedge_instrument = config.child("markets").child("hedge").get<std::string>("name");
        mapping::InstrumentInfo okx_instrument_info = mapping::getInstrumentInfo(hedge_instrument);
        return OkxPositionManager{
            config.child("trading_control").get<bool>("live_trading_enabled"),
            config.child("okx_position").get<double>("max_position", 0.0),
            config.child("okx_position").get<double>("base_position", 0.0),
            config.child("markets").child("hedge").child("tick_sizes").get<double>("quantity"),
            config.child("okx_recon").get<double>("tolerable_threshold", 1.0),
            config.child("okx_recon").get<uint32_t>("max_mismatch_cnt", 3),
            config.child("okx_recon").get<uint32_t>("max_failure_query_cnt", 5),
            config.child("okx_recon").get<uint32_t>("retry_interval_on_failure_ms", 2000),
            config.child("okx_recon").get<uint32_t>("normal_recon_interval_ms", 5000),
            config.child("okx_recon").get<uint32_t>("retry_interval_on_mismatch_ms", 3000),
            okx_instrument_info.category,
            okx_instrument_info.instrument,
            config.child("markets").child("hedge").child("exchange_keys").get<std::string>("api_key", ""),
            config.child("markets").child("hedge").child("exchange_keys").get<std::string>("api_secret", ""),
            config.child("markets").child("hedge").child("exchange_keys").get<std::string>("api_passphrase", "")};
    }

    static ByBitOrderManager create_bybit_order_manager(const Configuration& config,
                                                        ByBitPositionManager& position_manager) {
        return ByBitOrderManager{
            config.child("trading_control").get<bool>("live_trading_enabled"),
            Connections::getByBitProxy(),
            config.child("exchange_stability").get<uint32_t>("ws_reconnection_retry_limit", 10),
            config.child("markets").child("quote").get<uint32_t>("number_of_orders_to_track", 100),
            config.child("markets").child("quote").child("exchange_keys").get<std::string>("api_key"),
            config.child("markets").child("quote").child("exchange_keys").get<std::string>("api_secret"),
            position_manager};
    }

    static OkxOrderManager create_okx_order_manager(const Configuration& config, OkxPositionManager& position_manager) {
        std::string hedge_instrument = config.child("markets").child("hedge").get<std::string>("name", "");
        mapping::InstrumentInfo okx_instrument_info = mapping::getInstrumentInfo(hedge_instrument);

        return OkxOrderManager{
            config.child("trading_control").get<bool>("live_trading_enabled"),
            config.child("markets").child("hedge").get<uint32_t>("number_of_orders_to_track", 100),
            config.child("exchange_stability").get<uint32_t>("ws_reconnection_retry_limit", 10),
            Connections::getOkxProxy(),
            config.child("markets").child("hedge").child("exchange_keys").get<std::string>("api_key"),
            config.child("markets").child("hedge").child("exchange_keys").get<std::string>("api_secret"),
            config.child("markets").child("hedge").child("exchange_keys").get<std::string>("api_passphrase"),
            okx_instrument_info.instrument,
            position_manager};
    }

    static ByBitFills create_bybit_fills_manager(const Configuration& config, ByBitOrderManager& bybit_order_manager) {
        return ByBitFills{
            config.child("trading_control").get<bool>("live_trading_enabled"),
            Connections::getByBitProxy(),
            config.child("markets").child("quote").child("exchange_keys").get<std::string>("api_key", ""),
            config.child("markets").child("quote").child("exchange_keys").get<std::string>("api_secret", ""),
            config.child("markets").child("quote").get<uint32_t>("number_of_orders_to_track", 100),
            config.child("exchange_stability").get<uint32_t>("ws_reconnection_retry_limit", 10),
            bybit_order_manager};
    }

    static BinanceWebSocketClient create_binance_ws_client(const Configuration& config) {
        const bool is_live_trading = config.child("trading_control").get<bool>("live_trading_enabled");
        std::string binance_uri;
        if(is_live_trading) {
            binance_uri = Connections::getBinanceLiveMarket();
            std::string binance_instr = config.child("quoting_reference_price").get<std::string>("source");
            mapping::InstrumentInfo instr = mapping::getInstrumentInfo(binance_instr);
            binance_uri += "/" + instr.instrument + "@bookTicker";
        } else {
            binance_uri = Connections::getBinanceMockMarket();
        }
        return BinanceWebSocketClient{
            is_live_trading,
            config.child("exchange_stability").get<uint32_t>("ws_reconnection_retry_limit", 10),
            binance_uri,
            Connections::getBinanceProxy(),
            config.child("quoting_reference_price").get<std::string>("source")};
    };

    static ByBitWebSocketClient create_bybit_ws_client(const Configuration& config) {
        const bool is_live_trading = config.child("trading_control").get<bool>("live_trading_enabled");
        return ByBitWebSocketClient{
            is_live_trading ? Connections::getByBitLiveMarket() : Connections::getByBitMockMarket(),
            Connections::getByBitProxy(),
            config.child("markets").child("quote").get<std::string>("name"),
            config.child("exchange_stability").get<uint32_t>("ws_reconnection_retry_limit", 10),
            config.child("markets").child("quote").child("exchange_keys").get<std::string>("api_key"),
            config.child("markets").child("quote").child("exchange_keys").get<std::string>("api_secret"),
        };
    }

    static OKXWebSocketClient create_okx_ws_client(const Configuration& config) {
        const bool is_live_trading = config.child("trading_control").get<bool>("live_trading_enabled");
        return OKXWebSocketClient{
            is_live_trading ? Connections::getOkxLiveMarket() : Connections::getOkxMockMarket(),
            Connections::getOkxProxy(),
            config.child("markets").child("hedge").get<std::string>("name"),
            config.child("exchange_stability").get<uint32_t>("ws_reconnection_retry_limit", 10),
            config.child("markets").child("hedge").child("exchange_keys").get<std::string>("api_key"),
            config.child("markets").child("hedge").child("exchange_keys").get<std::string>("api_secret"),
            config.child("markets").child("hedge").child("exchange_keys").get<std::string>("api_passphrase"),
        };
    }


    static EventProcessor create_event_processor() {
        return EventProcessor{};
    }

    /* -------------------------------------------------------------------------- */
    /*                             START UP FUNCTIONS                             */
    /* -------------------------------------------------------------------------- */

    void start_all_ws() {
        threads_.binance = std::thread([this] { binance_ws_.start(); });
        threads_.bybit = std::thread([this] { bybit_ws_.start(); });
        threads_.okx = std::thread([this] { okx_ws_.start(); });
        threads_.okx_order_manager = std::thread([this] { okx_order_manager_.run(); });
        threads_.bybit_order_manager = std::thread([this] { bybit_order_manager_.run(); });
        threads_.bybit_fills = std::thread([this] { bybit_fills_manager_.setupRoutingConnection(); });
        log_action_pass("start_all_ws");
    }

    void start_timer() {
        const auto frequency = config_.child("exchange_stability").get<uint64_t>("websocket_heartbeat_ms", 10000);
        timer_.start(frequency);
        log_action_pass("start_timer", f("frequency", frequency));
    }

    /* -------------------------------------------------------------------------- */
    /*                             CLEAN UP FUNCTIONS                             */
    /* -------------------------------------------------------------------------- */

    void stop_trading_managers() {
        try {
            okx_order_manager_.stop();
            log_action_pass("stop_okx_order_manager");
            bybit_fills_manager_.stop();
            log_action_pass("stop_bybit_fills_manager");
            bybit_order_manager_.stop();
            log_action_pass("stop_bybit_order_manager");
            bybit_position_manager_.stop();
            log_action_pass("stop_bybit_position_manager");
            okx_position_manager_.stop();
            log_action_pass("stop_okx_position_manager");
        } catch(const std::exception& e) {
            log_action_fail<LogLevel::ERROR>("stop_trading_managers", e.what());
            throw;
        }
    }

    void stop_all_ws() {
        try {
            bybit_ws_.stop();
            okx_ws_.stop();
            binance_ws_.stop();
            log_action_pass("stop_all_ws");
        } catch(const std::exception& e) {
            log_action_fail<LogLevel::ERROR>("stop_all_ws", e.what());
            throw;
        }
    }

    void join_threads() {
        try {
            auto join_if_active = [](std::thread& thread) {
                if(thread.joinable()) {
                    thread.join();
                }
            };
            join_if_active(threads_.okx);
            join_if_active(threads_.binance);
            join_if_active(threads_.bybit);
            join_if_active(threads_.okx_order_manager);
            join_if_active(threads_.bybit_order_manager);
            join_if_active(threads_.bybit_fills);
            timer_.stop();
            log_action_pass("join_threads");
        } catch(const std::exception& e) {
            log_action_fail<LogLevel::ERROR>("join_threads", e.what());
            throw;
        }
    }

    void cleanup() {
        // Stop trading managers
        stop_trading_managers();
        event_processor_.stop();
        stop_all_ws();
        join_threads();
        log_action_pass("cleanup");
    }

    void send_ws_heartbeats() {
        log_event("send_ws_heartbeats");
        bybit_order_manager_.send_heartbeat();
        okx_order_manager_.send_heartbeat();
        bybit_fills_manager_.send_heartbeat();
        bybit_ws_.send_heartbeat();
        okx_ws_.send_heartbeat();
    }

    void setup_callbacks() {
        // Binance WebSocket
        binance_ws_.setMarketDataUpdateCallback(callback_adapter_.create_binance_market_update_callback());
        binance_ws_.setWebSocketStatusUpdateCallback(callback_adapter_.create_ws_disconnected_callback());
        // Bybit WebSocket
        bybit_ws_.setMarketDataUpdateCallback(callback_adapter_.create_bybit_market_update_callback());
        bybit_ws_.setWebSocketStatusUpdateCallback(callback_adapter_.create_ws_disconnected_callback());
        // Okx WebSocket
        okx_ws_.setMarketDataUpdateCallback(callback_adapter_.create_okx_market_update_callback());
        okx_ws_.setWebSocketStatusUpdateCallback(callback_adapter_.create_ws_disconnected_callback());
        // Bybit Order Manager
        bybit_order_manager_.setOrderStatusUpdateCallback(callback_adapter_.create_bybit_order_update_callback());
        bybit_order_manager_.setWebsocketHealthCallback(callback_adapter_.create_ws_disconnected_callback());
        // Okx Order Manager
        okx_order_manager_.setOrderStatusUpdateCallback(callback_adapter_.create_okx_order_update_callback());
        okx_order_manager_.setWebsocketHealthCallback(callback_adapter_.create_ws_disconnected_callback());
        // Bybit Fills Manager
        bybit_fills_manager_.setOrderStatusUpdateCallback(callback_adapter_.create_bybit_order_update_callback());
        bybit_fills_manager_.setWebSocketStatusUpdateCallback(callback_adapter_.create_ws_disconnected_callback());
        // Timer
        timer_.addCallback([this]() { send_ws_heartbeats(); });
        log_action_pass("setup_callbacks");
    }


    static void handle_start_trading() {}

    static void handle_stop_trading(const StopTradingEventData& data) {}

    static void handle_bybit_market_update() {}

    static void handle_binance_market_update() {}

    static void handle_okx_market_update() {}

    static void handle_bybit_order_update(const OrderUpdateEventData& order) {}

    static void handle_okx_order_update(const OrderUpdateEventData& order) {}

    static void handle_position_recon(const PositionReconEventData& data) {}

    static void handle_ws_disconnected(const WsDisconnectedEventData& data) {}

    Configuration config_;
    ByBitPositionManager bybit_position_manager_{create_bybit_position_manager(config_)};
    OkxPositionManager okx_position_manager_{create_okx_position_manager(config_)};
    ByBitOrderManager bybit_order_manager_{create_bybit_order_manager(config_, bybit_position_manager_)};
    OkxOrderManager okx_order_manager_{create_okx_order_manager(config_, okx_position_manager_)};

    ByBitFills bybit_fills_manager_{create_bybit_fills_manager(config_, bybit_order_manager_)};
    Timer timer_{};
    EventProcessor event_processor_{create_event_processor()};
    CallbackAdapter callback_adapter_{event_processor_};
    Threads threads_{};

    // WebSocket Clients
    BinanceWebSocketClient binance_ws_{create_binance_ws_client(config_)};
    ByBitWebSocketClient bybit_ws_{create_bybit_ws_client(config_)};
    OKXWebSocketClient okx_ws_{create_okx_ws_client(config_)};
};
