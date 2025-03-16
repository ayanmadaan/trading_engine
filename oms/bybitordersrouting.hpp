#pragma once
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <chrono>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp> // For TLS client (OKX)
// #include "ordermanager.hpp"
#include "../lib/json.hpp"
#include "../utils/connections.hpp"
#include "../utils/helper.hpp"
#include "../utils/logger.hpp"
#include "../utils/requests.hpp"
#include "../utils/staticparams.hpp"
#include <cmath>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

typedef websocketpp::client<websocketpp::config::asio_tls_client> client_tls;

using nlohmann::json;
using websocketpp::connection_hdl;
using message_ptr = client_tls::message_ptr;

class ByBitOrderRouter {
public:
    using OrderUpdateCallback = std::function<void(std::string message)>;
    ByBitOrderRouter(const bool trading_mode,
                     const std::string& proxy_uri,
                     const uint32_t retry_limit,
                     const std::string api_key,
                     const std::string api_secret,
                     OrderUpdateCallback callback)
        : proxy_uri(proxy_uri)
        , retry_limit(retry_limit)
        , api_key(api_key)
        , api_secret(api_secret)
        , orderUpdateCallback(std::move(callback))
        , argsObject(rapidjson::kObjectType)
        , argsArray(rapidjson::kArrayType) {
        buffer.Reserve(256);
        if(trading_mode) {
            uri = Connections::getByBitLiveOrder();
        } else {
            uri = Connections::getByBitTestOrder();
        }
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
            LoggerSingleton::get().infra().error("error in setup routing connection: ", e.what());
        }

        routing_client.set_close_handler([this](websocketpp::connection_hdl hdl) {
            LoggerSingleton::get().infra().warning("websocket connection closed");
            m_wsState = false;
            handle_disconnect();
        });

        routing_client.set_fail_handler([this](websocketpp::connection_hdl hdl) {
            LoggerSingleton::get().infra().error("websocket connection failed");
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
            LoggerSingleton::get().infra().info("websocket run exception: ", e.what());
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
            LoggerSingleton::get().infra().error("error stopping bybitrouter: ", e.what());
        }
    }

    void handle_disconnect() {
        LoggerSingleton::get().infra().error("bybit trades stream disconnect");
        if(reconnect_attempt + 1 > retry_limit) {
            std::string message = "connection_end";
            orderUpdateCallback(message);
        } else {
            std::string message = "disconnect";
            orderUpdateCallback(message);
            schedule_reconnect();
        }
    }

    void schedule_reconnect() {
        LOG_INFRA_DEBUG("action=reconnecting exchange=bybit stream=trades");
        connect_to_websocket();
    }

    void authenticate() {
        websocketpp::lib::error_code ec;
        long long timestamp = helper::get_current_timestamp_ms();
        uint64_t expires = (timestamp + 1000);
        std::string message = "GET/realtime" + std::to_string(expires);
        std::string signature = helper::generate_signature_bybit(api_secret, message);
        nlohmann::json auth_payload = {{"op", "auth"}, {"args", {api_key, expires, signature}}};
        routing_client.send(routing_hdl, auth_payload.dump(), websocketpp::frame::opcode::text, ec);
    }

    bool isWebsocketReady() const { return this->m_wsState; }

    bool send_heartbeat() {
        try {
            nlohmann::json ping_msg = {{"op", "ping"}};
            LOG_INFRA_DEBUG("bybit trades channel heartbeat: ping");
            routing_client.send(routing_hdl, ping_msg.dump(), websocketpp::frame::opcode::text);
            return true;
        } catch(const std::exception& e) {
            LoggerSingleton::get().infra().error("action=heartbeat exchage=bybit stream=trades result=fail reason=",
                                                 e.what());
            return false;
        }
    }

    void onOrderUpdateMessage(const std::string& message) {
        json parsedJson = json::parse(message);
        if(parsedJson.contains("op") && parsedJson["op"] == "pong") {
            LOG_INFRA_DEBUG("bybit trades channel heartbeat: pong");
            LOG_INFRA_DEBUG("action=heartbeat exchange=bybit stream=trades result=pass");
            return;
        }
        LoggerSingleton::get().plain().ws_broadcast("bybit order manager update: ", parsedJson.dump());
        if(parsedJson.contains("retCode") && parsedJson["retCode"] == 0) {
            if(parsedJson.contains("retMsg") && parsedJson["retMsg"] == "OK") {
                if(parsedJson.contains("op") && parsedJson["op"] == "auth") {
                    this->m_wsState = true;
                }
            }
        }
        orderUpdateCallback(message);
    }

    uint64_t sendOrder(double price,
                       double qty,
                       bool buy,
                       uint64_t reqId,
                       std::string instrumentId,
                       std::string ordType = "limit",
                       std::string tdMode = "cross",
                       bool banAmend = true) {
        uint64_t ret = helper::get_current_timestamp_ns();
        std::string clientOrderId1 = std::to_string(ret);
        std::string ts = std::to_string(helper::get_current_timestamp_ms());
        std::string side1 = buy ? "Buy" : "Sell";
        if(instrumentId == "BTCUSDT")
            qty = std::round(qty / (bybit::BTCUSDT::btcPerpCtVal) * 1e6) / 1e6;
        else if(instrumentId == "DOGEUSDT")
            qty = std::round(qty / (bybit::DOGEUSDT::dogePerpCtVal) * 1e6) / 1e6;
        nlohmann::json place_order_payload_nlohmann = {{"header", {{"X-BAPI-TIMESTAMP", ts}}},
                                                       {"reqId", std::to_string(reqId)},
                                                       {"op", "order.create"},
                                                       {"args",
                                                        {{{"symbol", instrumentId},
                                                          {"side", side1},
                                                          {"orderLinkId", clientOrderId1},
                                                          {"qty", std::to_string(qty)},
                                                          {"category", "linear"}}}}};
        if(ordType == "market") {
            place_order_payload_nlohmann["args"][0]["orderType"] = "Market";
        } else {
            place_order_payload_nlohmann["args"][0]["orderType"] = "Limit";
            place_order_payload_nlohmann["args"][0]["price"] = std::to_string(price);
        }
        if(ordType == "post_only") {
            place_order_payload_nlohmann["args"][0]["timeInForce"] = "PostOnly";
        }
        std::string payload_str_nlohmann = place_order_payload_nlohmann.dump();
        LoggerSingleton::get().plain().ws_request("new order payload: ", payload_str_nlohmann);
        try {
            routing_client.send(routing_hdl, std::move(payload_str_nlohmann), websocketpp::frame::opcode::text);
        } catch(const websocketpp::exception& e) {
            LoggerSingleton::get().infra().error("websocket send error: ", e.what());
            return 0;
        }
        return ret;
    }

    uint64_t modifyOrder(uint64_t orderId, double newQty, double newPrice, uint64_t reqId, std::string instrumentId) {
        std::string ts = std::to_string(helper::get_current_timestamp_ms());
        if(instrumentId == "BTCUSDT")
            newQty = std::round(newQty / (bybit::BTCUSDT::btcPerpCtVal) * 1e6) / 1e6;
        else if(instrumentId == "DOGEUSDT")
            newQty = std::round(newQty / (bybit::DOGEUSDT::dogePerpCtVal) * 1e6) / 1e6;
        nlohmann::json modify_order_payload_nlohmann = {{"header", {{"X-BAPI-TIMESTAMP", ts}}},
                                                        {"reqId", std::to_string(reqId)},
                                                        {"op", "order.amend"},
                                                        {"args",
                                                         {{{"category", "linear"},
                                                           {"symbol", instrumentId},
                                                           {"orderLinkId", std::to_string(orderId)},
                                                           {"qty", std::to_string(newQty)},
                                                           {"price", std::to_string(newPrice)}}}}};
        std::string payload_str_nlohmann = modify_order_payload_nlohmann.dump();
        LoggerSingleton::get().plain().ws_request("modify order payload: ", payload_str_nlohmann);
        try {
            routing_client.send(routing_hdl, std::move(payload_str_nlohmann), websocketpp::frame::opcode::text);
        } catch(const websocketpp::exception& e) {
            LoggerSingleton::get().infra().error("websocket send error: ", e.what());
            return 0;
        }
        return orderId;
    }

    uint64_t sendCancelOrder(uint64_t clOrdId, uint64_t reqId, std::string instrumentId) {
        std::string ts = std::to_string(helper::get_current_timestamp_ms());
        nlohmann::json cancel_order_payload_nlohmann = {
            {"header", {{"X-BAPI-TIMESTAMP", ts}}},
            {"reqId", std::to_string(reqId)},
            {"op", "order.cancel"},
            {"args", {{{"category", "linear"}, {"symbol", instrumentId}, {"orderLinkId", std::to_string(clOrdId)}}}}};
        std::string payload_str = cancel_order_payload_nlohmann.dump();
        try {
            LoggerSingleton::get().plain().ws_request("cancel order payload: ", payload_str);
            routing_client.send(routing_hdl, std::move(payload_str), websocketpp::frame::opcode::text);
        } catch(const websocketpp::exception& e) {
            LoggerSingleton::get().infra().error("websocket send error: ", e.what());
            return 0;
        }
        return clOrdId;
    }

private:
    bool m_wsState = false;
    const uint32_t retry_limit = 0;
    uint32_t reconnect_attempt = 0;
    std::string uri;
    std::string proxy_uri;
    rapidjson::StringBuffer buffer;
    rapidjson::Value argsArray;
    rapidjson::Value argsObject;
    OrderUpdateCallback orderUpdateCallback;

    connection_hdl routing_hdl;
    client_tls routing_client;

    std::string connId = "";
    const std::string api_key = "";
    const std::string api_secret = "";
};
