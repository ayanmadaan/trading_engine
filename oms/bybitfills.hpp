#pragma once
#include "../lib/json.hpp"
#include "../utils/helper.hpp"
#include "../utils/logger.hpp"
#include "../utils/requests.hpp"
#include "bybitordermanager.hpp"
#include "orderhandler.hpp"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <chrono>
#include <cmath>
#include <mutex>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp> // For TLS client (OKX)

typedef websocketpp::client<websocketpp::config::asio_tls_client> client_tls;

using nlohmann::json;
using websocketpp::connection_hdl;
using message_ptr = client_tls::message_ptr;

class ByBitFills {
public:
    using OrderStatusUpdateCallback = std::function<void(OrderHandler&)>;
    using WebSocketStatusUpdateCallback = std::function<void(bool)>;

    explicit ByBitFills(const bool trading_mode,
                        const std::string& proxy_uri,
                        const std::string apiKey,
                        const std::string apiSecret,
                        const uint32_t track_order_cnt,
                        const uint32_t retry_limit,
                        ByBitOrderManager& manager)
        : proxy_uri(proxy_uri)
        , apiKey(apiKey)
        , apiSecret(apiSecret)
        , m_trackOrderCnt(track_order_cnt)
        , retry_limit(retry_limit)
        , argsObject(rapidjson::kObjectType)
        , argsArray(rapidjson::kArrayType)
        , bybitOrderManager(manager) {
        buffer.Reserve(256);
        if(trading_mode) {
            uri = Connections::getByBitLiveFills();
        } else {
            uri = Connections::getByBitTestFills();
        }
    }

    void setOrderStatusUpdateCallback(OrderStatusUpdateCallback callback) {
        std::lock_guard<std::mutex> lock(m_mutex);
        orderUpdateCallback = std::move(callback);
    }

    void setWebSocketStatusUpdateCallback(WebSocketStatusUpdateCallback callback) {
        std::lock_guard<std::mutex> lock(m_mutex);
        websocketUpdateCallback = std::move(callback);
    }

    void setupRoutingConnection() {
        routing_client.init_asio();
        routing_client.clear_access_channels(websocketpp::log::alevel::all); // Disables all access logging
        routing_client.clear_error_channels(websocketpp::log::elevel::all); // Disables all error logging
        routing_client.set_tls_init_handler([](websocketpp::connection_hdl) {
            auto ctx = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);
            try {
                ctx->set_options(boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::no_sslv2 |
                                 boost::asio::ssl::context::no_sslv3 | boost::asio::ssl::context::single_dh_use);
            } catch(std::exception& e) {
                LoggerSingleton::get().infra().error("error in tls initialization: ", e.what());
            }
            return ctx;
        });
        routing_client.set_message_handler([this](websocketpp::connection_hdl hdl, client_tls::message_ptr msg) {
            onOrderUpdateMessage(msg->get_payload());
        });
        try {
            routing_client.set_open_handler([this](websocketpp::connection_hdl hdl) {
                routing_hdl = hdl; // Store the connection handle
                authenticate();
            });

        } catch(std::exception e) {
            LoggerSingleton::get().infra().error("failed to set open handler due to: ", e.what());
        }

        routing_client.set_close_handler([this](websocketpp::connection_hdl hdl) {
            LoggerSingleton::get().infra().warning("bybit fills websocket connection closed");
            m_wsState = false;
            handle_disconnect();
        });

        routing_client.set_fail_handler([this](websocketpp::connection_hdl hdl) {
            LoggerSingleton::get().infra().error("bybit fills websocket connection failed");
            m_wsState = false;
            handle_disconnect();
        });

        connect_to_websocket();
    }

    void connect_to_websocket() {
        reconnect_attempt += 1;
        websocketpp::lib::error_code ec;
        client_tls::connection_ptr con = routing_client.get_connection(uri, ec);
        if(ec) {
            LoggerSingleton::get().infra().error("connection failed: ", ec.message());
            return;
        }
        if(proxy_uri != "") {
            LoggerSingleton::get().infra().info("proxy uri: ", proxy_uri);
            con->set_proxy(proxy_uri);
        }

        routing_client.connect(con);
        try {
            routing_client.run();
        } catch(const std::exception& e) {
            LoggerSingleton::get().infra().error("websocket run exception: ", e.what());
        }
    }

    void stop() {
        try {
            if(!routing_hdl.expired()) { // Check if we have a valid connection
                // Close the websocket connection gracefully
                routing_client.close(routing_hdl, websocketpp::close::status::normal, "Shutting down");
            }
            // Stop the ASIO io_service
            routing_client.stop();
        } catch(const std::exception& e) {
            LoggerSingleton::get().infra().error("error in stopping okx order router: ", e.what());
        }
    }

    void handle_disconnect() {
        LoggerSingleton::get().infra().error("bybit fills stream disconnect");
        if(reconnect_attempt + 1 > retry_limit) {
            if(websocketUpdateCallback) {
                websocketUpdateCallback(true);
            }
        } else {
            if(websocketUpdateCallback) {
                websocketUpdateCallback(false);
            }
            schedule_reconnect();
        }
    }

    void send_heartbeat() {
        try {
            nlohmann::json ping_msg = {{"op", "ping"}};
            LOG_INFRA_DEBUG("bybit fills channel heartbeat: ping");
            routing_client.send(routing_hdl, ping_msg.dump(), websocketpp::frame::opcode::text);
        } catch(const std::exception& e) {
            LoggerSingleton::get().infra().error("action=heartbeat exchage=bybit stream=fills result=fail reason=",
                                                 e.what());
            handle_disconnect();
        }
    }

    void schedule_reconnect() {
        LOG_INFRA_DEBUG("action=reconnecting exchange=bybit stream=fills");
        connect_to_websocket();
    }

    void authenticate() {
        websocketpp::lib::error_code ec;
        long long timestamp = helper::get_current_timestamp_ms();
        uint64_t expires = (timestamp + 1000);
        std::string message = "GET/realtime" + std::to_string(expires);
        std::string signature = helper::generate_signature_bybit(apiSecret, message);
        nlohmann::json auth_payload = {{"op", "auth"}, {"args", {apiKey, expires, signature}}};
        routing_client.send(routing_hdl, auth_payload.dump(), websocketpp::frame::opcode::text, ec);
    }

    bool isWebsocketReady() const { return m_wsState; }

    void eraseReqId(uint64_t clOrderId) {
        // std::erase_if(bybitOrderManager.reqId_to_clOrderId, [&](const auto& pair) {
        //     if (pair.second == clOrderId) {
        //         return true;  // Mark for erasure
        //     }
        //     return false;
        // });
    }

    void maintainOrderLimit() {
        std::lock_guard<std::mutex> lock(m_mutex);
        while(bybitOrderManager.cancelQueue.size() > m_trackOrderCnt) {
            uint64_t clOrderId = bybitOrderManager.cancelQueue.front();
            auto it = bybitOrderManager.orderMap.find(clOrderId);
            if(it != bybitOrderManager.orderMap.end()) {
                bybitOrderManager.orderMap.erase(clOrderId);
            }
            bybitOrderManager.cancelQueue.pop();
            // eraseReqId(clOrderId);
        }

        while(bybitOrderManager.rejectedQueue.size() > m_trackOrderCnt) {
            uint64_t clOrderId = bybitOrderManager.rejectedQueue.front();
            auto it = bybitOrderManager.orderMap.find(clOrderId);
            if(it != bybitOrderManager.orderMap.end()) {
                bybitOrderManager.orderMap.erase(clOrderId);
            }
            bybitOrderManager.rejectedQueue.pop();
            // eraseReqId(clOrderId);
        }

        while(bybitOrderManager.filledQueue.size() > m_trackOrderCnt) {
            uint64_t clOrderId = bybitOrderManager.filledQueue.front();
            auto it = bybitOrderManager.orderMap.find(clOrderId);
            if(it != bybitOrderManager.orderMap.end()) {
                bybitOrderManager.orderMap.erase(clOrderId);
            }
            bybitOrderManager.filledQueue.pop();
            // eraseReqId(clOrderId);
        }
    }

    void onOrderUpdateMessage(const std::string& message) {
        LoggerSingleton::get().plain().ws_broadcast("fills message: ", message);
        json parsedJson = json::parse(message);
        if(parsedJson.contains("op") && parsedJson["op"] == "pong") {
            LOG_INFRA_DEBUG("bybit fills channel heartbeat: pong");
            LOG_INFRA_DEBUG("action=heartbeat exchange=bybit stream=fills result=pass");
            return;
        }
        if(parsedJson.contains("ret_msg") && parsedJson["ret_msg"] == "") {
            if(parsedJson.contains("op") && parsedJson["op"] == "auth") {
                std::string ORDER_SUBSCRIBE_MSG = requests::getByBitFillsSubscribeMessage();
                routing_client.send(routing_hdl, std::move(ORDER_SUBSCRIBE_MSG), websocketpp::frame::opcode::text);
                m_wsState = true;
                return;
            }
            if((!requestSent) && parsedJson.contains("op") && parsedJson["op"] == "subscribe") {
                std::string EXEC_SUBSCRIBE_MSG = requests::getByBitExecutionSubscribeMessage();
                routing_client.send(routing_hdl, std::move(EXEC_SUBSCRIBE_MSG), websocketpp::frame::opcode::text);
                requestSent = true;
                return;
            }
        } else if(parsedJson.contains("topic") && parsedJson["topic"] == "order") {
            for(const auto& orderData : parsedJson["data"]) {
                uint64_t clOrderId = 0;
                if(orderData["orderLinkId"] != "") {
                    clOrderId = std::stoull(orderData["orderLinkId"].get<std::string>());
                }
                std::string status = orderData["orderStatus"];
                std::string reject = orderData["rejectReason"];
                auto iterator = this->bybitOrderManager.orderMap.find(clOrderId);
                if(iterator == (this->bybitOrderManager.orderMap.end())) {
                    LoggerSingleton::get().infra().warning(
                        "order not placed from this strat run with client order id: ", clOrderId);
                } else {
                    auto order = iterator->second;
                    if(reject != "EC_NoError") {
                        order->m_status = OrderStatus::REJECTED;
                        if(reject == "EC_InvalidSymbolStatus") {
                            order->m_reason = RejectReason::INVALID_INSTRUMENT;
                            bybitOrderManager.rejectedQueue.push(order->m_clientOrderId);
                            maintainOrderLimit();
                        } else if(reject == "EC_OrderNotExist") {
                            order->m_reason = RejectReason::ORDER_DOES_NOT_EXIST_ON_EXCH_ORDERBOOK;
                            bybitOrderManager.rejectedQueue.push(order->m_clientOrderId);
                            maintainOrderLimit();
                        } else if(reject == "EC_PostOnlyWillTakeLiquidity") {
                            order->m_reason = RejectReason::POST_ONLY_WILL_TAKE_LIQUIDITY;
                            bybitOrderManager.rejectedQueue.push(order->m_clientOrderId);
                            maintainOrderLimit();
                        } else if(reject == "EC_PerCancelRequest") {
                            order->m_reason = RejectReason::NONE;
                            order->m_status = OrderStatus::CANCELED;
                            if(orderData.contains("createdTime")) {
                                order->m_cancelOrderOnExchTS =
                                    std::stoull(orderData["createdTime"].get<std::string>()) * 1000000ULL;
                                order->m_cancelOrderConfirmationTS = helper::get_current_timestamp_ns();
                            }
                            bybitOrderManager.cancelQueue.push(order->m_clientOrderId);
                            maintainOrderLimit();
                        } else if(reject == "EC_OrigClOrdIDDoesNotExist") {
                            order->m_reason = RejectReason::ORDER_DOES_NOT_EXIST_ON_EXCH_ORDERBOOK;
                            order->m_status = OrderStatus::REJECTED;
                            bybitOrderManager.rejectedQueue.push(order->m_clientOrderId);
                            maintainOrderLimit();
                        } else {
                            order->m_reason = RejectReason::UNKNOWN_ERROR;
                            bybitOrderManager.rejectedQueue.push(order->m_clientOrderId);
                            maintainOrderLimit();
                        }
                        if(orderUpdateCallback) {
                            orderUpdateCallback(*order);
                        }
                    } else if(orderData["orderStatus"] == "New") {
                        order->m_reason = RejectReason::NONE;
                        order->m_status = OrderStatus::LIVE;
                        order->m_orderHasBeenLive = true;
                        order->m_qtyOnExch = std::stod(orderData["leavesQty"].get<std::string>());
                        order->m_priceOnExch = std::stod(orderData["price"].get<std::string>());
                        order->m_cumFilledQty = std::stod(orderData["cumExecQty"].get<std::string>());
                        if(orderData.contains("createdTime")) {
                            if(order->m_newOrderOnExchTS == 0) {
                                order->m_newOrderOnExchTS =
                                    std::stoull(orderData["createdTime"].get<std::string>()) * 1000000ULL;
                                order->m_newOrderConfirmationTS = helper::get_current_timestamp_ns();
                            } else {
                                order->m_modifyOrderOnExchTS =
                                    std::stoull(orderData["createdTime"].get<std::string>()) * 1000000ULL;
                                order->m_modifyOrderConfirmationTS = helper::get_current_timestamp_ns();
                            }
                        }
                        if(orderUpdateCallback) {
                            orderUpdateCallback(*order);
                        }
                    } else if(orderData["orderStatus"] == "PartiallyFilled") {
                        order->m_reason = RejectReason::NONE;
                        order->m_status = OrderStatus::PARTIALLY_FILLED;
                        /*
                        order->m_qtyOnExch = std::stod(orderData["leavesQty"].get<std::string>());

                        double currFillQty = order->m_cumFilledQty;
                        order->m_cumFilledQty = std::stod(orderData["cumExecQty"].get<std::string>());
                        currFillQty = order->m_cumFilledQty - currFillQty;
                        order->m_fillSz = currFillQty;

                        double fillPx = std::stod(orderData["price"].get<std::string>());
                        order->m_fillPx = fillPx;

                        double currFillFee = order->m_fillFee;
                        order->m_fillFee = std::stod(orderData["cumExecFee"].get<std::string>()) - currFillFee;
                        order->m_cumFee = std::stod(orderData["cumExecFee"].get<std::string>());

                        if (orderData["orderType"] == "Market") {
                            order->m_fillMaker = false;
                        }
                        else {
                            order->m_fillMaker = true;
                        }
                        */
                        /*
                        if(order->m_side) {
                            bybitOrderManager.m_exposureQty += currFillQty;
                        } else {
                            bybitOrderManager.m_exposureQty -= currFillQty;
                        }
                        */
                    } else if(orderData["orderStatus"] == "Filled") {
                        order->m_reason = RejectReason::NONE;
                        order->m_status = OrderStatus::FILLED;
                        /*
                        order->m_qtyOnExch = std::stod(orderData["leavesQty"].get<std::string>());

                        double currFillQty = order->m_cumFilledQty;
                        order->m_cumFilledQty = std::stod(orderData["cumExecQty"].get<std::string>());
                        currFillQty = order->m_cumFilledQty - currFillQty;
                        order->m_fillSz = currFillQty;

                        double fillPx = std::stod(orderData["price"].get<std::string>());
                        order->m_fillPx = fillPx;
                        double currFillFee = order->m_fillFee;
                        order->m_fillFee = std::stod(orderData["cumExecFee"].get<std::string>()) - currFillFee;
                        order->m_cumFee = std::stod(orderData["cumExecFee"].get<std::string>());
                        bybitOrderManager.m_positionManager.update_position_by_fillsz(currFillQty, order->m_side);
                        */
                        /*
                        if(order->m_side) {
                            bybitOrderManager.m_exposureQty += (currFillQty);
                        } else {
                            bybitOrderManager.m_exposureQty -= (currFillQty);
                        }
                        */
                        /*
                            if(orderData.contains("closedPnl")) {
                                double closedPnl = std::stod(orderData["closedPnl"].get<std::string>());
                                bybitOrderManager.m_realisedPnl += closedPnl;
                            }
                            */
                    } else if(orderData["orderStatus"] == "Cancelled") {
                        order->m_reason = RejectReason::NONE;
                        order->m_status = OrderStatus::CANCELED;
                        if(orderData.contains("createdTime")) {
                            order->m_cancelOrderOnExchTS =
                                std::stoull(orderData["createdTime"].get<std::string>()) * 1000000ULL;
                            order->m_cancelOrderConfirmationTS = helper::get_current_timestamp_ns();
                        }
                        bybitOrderManager.cancelQueue.push(order->m_clientOrderId);
                        maintainOrderLimit();
                        if(orderUpdateCallback) {
                            orderUpdateCallback(*order);
                        }
                    }
                }
            }
        } else if(parsedJson.contains("topic") && parsedJson["topic"] == "execution") {
            for(const auto& execution : parsedJson["data"]) {
                uint64_t clOrderId = 0;
                if(execution["orderLinkId"] != "") {
                    clOrderId = std::stoull(execution["orderLinkId"].get<std::string>());
                }
                auto iterator = this->bybitOrderManager.orderMap.find(clOrderId);
                if(iterator == (this->bybitOrderManager.orderMap.end())) {
                    LoggerSingleton::get().infra().warning("order not placed from this strat run");
                } else {
                    auto order = iterator->second;
                    order->m_reason = RejectReason::NONE;

                    double fillFee = std::stod(execution["execFee"].get<std::string>());
                    double fillSz = std::stod(execution["execQty"].get<std::string>());
                    double fillPx = std::stod(execution["execPrice"].get<std::string>());
                    double leavesQty = std::stod(execution["leavesQty"].get<std::string>());
                    double fillPnl = std::stod(execution["execPnl"].get<std::string>());
                    bool isMaker = execution["isMaker"].get<bool>();
                    std::string transactionId = execution["execId"].get<std::string>();
                    order->m_transactionId = transactionId;
                    if(execution.contains("execTime")) {
                        order->m_executedTS = std::stoull(execution["execTime"].get<std::string>()) * 1000000ULL;
                    }
                    order->m_executeTSOnOms = helper::get_current_timestamp_ns();
                    if(leavesQty != 0) {
                        order->m_status = OrderStatus::PARTIALLY_FILLED;
                    } else {
                        order->m_status = OrderStatus::FILLED;
                        bybitOrderManager.filledQueue.push(order->m_clientOrderId);
                        maintainOrderLimit();
                    }

                    order->m_fillFee = fillFee;
                    order->m_cumFee += fillFee;
                    order->m_fillSz = fillSz;
                    order->m_cumFilledQty += fillSz;
                    order->m_qtyOnExch = leavesQty;
                    order->m_fillPx = fillPx;
                    order->m_fillMaker = isMaker;

                    bybitOrderManager.m_positionManager.update_position_by_fillsz(fillSz, order->m_side);
                    bybitOrderManager.m_realisedPnl += fillPnl;
                    if(orderUpdateCallback) {
                        orderUpdateCallback(*order);
                    }
                }
            }
        }
    }

private:
    bool requestSent = false;
    std::string uri;
    std::string proxy_uri;
    const std::string apiKey;
    const std::string apiSecret;
    uint32_t m_trackOrderCnt;
    rapidjson::StringBuffer buffer;
    rapidjson::Value argsArray;
    rapidjson::Value argsObject;

    OrderStatusUpdateCallback orderUpdateCallback;
    WebSocketStatusUpdateCallback websocketUpdateCallback;
    uint32_t reconnect_attempt = 0;
    uint32_t retry_limit = 0;

    connection_hdl routing_hdl;
    client_tls routing_client;

    std::string connId = "";
    bool m_wsState = false;
    std::mutex m_mutex;
    ByBitOrderManager& bybitOrderManager;
};
