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

static constexpr std::string_view BUY = "buy";
static constexpr std::string_view SELL = "sell";

class OkxOrderRouter {
public:
    using OrderUpdateCallback = std::function<void(std::string message)>;
    OkxOrderRouter(const bool trading_mode,
                   const std::string& proxy_uri,
                   const uint32_t retry_limit,
                   const std::string api_key,
                   const std::string api_secret,
                   const std::string api_passphrase,
                   const std::string instrument,
                   OrderUpdateCallback callback)
        : proxy_uri(proxy_uri)
        , retry_limit(retry_limit)
        , apiKey(api_key)
        , secretKey(api_secret)
        , passphrase(api_passphrase)
        , instrument(instrument)
        , orderUpdateCallback(std::move(callback))
        , argsObject(rapidjson::kObjectType)
        , argsArray(rapidjson::kArrayType) {
        if(trading_mode) {
            uri = Connections::getOkxLiveOrder();
        } else {
            uri = Connections::getOkxTestOrder();
        }
        buffer.Reserve(256);
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

        routing_client.set_open_handler([this](websocketpp::connection_hdl hdl) {
            routing_hdl = hdl; // Store the connection handle
            authenticate();
        });

        routing_client.set_close_handler([this](websocketpp::connection_hdl hdl) {
            LoggerSingleton::get().infra().warning("okx websocket connection closed");
            m_wsState = false;
            handle_disconnect();
        });

        routing_client.set_fail_handler([this](websocketpp::connection_hdl hdl) {
            LoggerSingleton::get().infra().error("okx websocket connection failed");
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
            LoggerSingleton::get().infra().error("error stopping okx order router: ", e.what());
        }
    }

    void handle_disconnect() {
        LoggerSingleton::get().infra().error("okx trades stream disconnect");
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
        LOG_INFRA_DEBUG("action=reconnecting exchange=okx stream=trades");
        connect_to_websocket();
    }

    void authenticate() {
        std::string timestamp = helper::get_current_timestamp();
        std::string method = "GET";
        std::string request_path = "/users/self/verify/";
        std::string body = "";
        std::string string_to_sign = helper::create_string_to_sign(timestamp, method, request_path, body);
        std::string signature = helper::generate_signature(secretKey, timestamp);
        std::string login_payload = "{\"op\": \"login\", "
                                    "\"args\": [{"
                                    "\"apiKey\": \"" +
                                    apiKey +
                                    "\", "
                                    "\"passphrase\": \"" +
                                    passphrase +
                                    "\", "
                                    "\"sign\": \"" +
                                    signature +
                                    "\", "
                                    "\"timestamp\": \"" +
                                    timestamp + "\"}]}";
        // std::cout << "Login payload: " << login_payload << std::endl;
        // std::string auth_message = generate_auth_message();
        routing_client.send(routing_hdl, std::move(login_payload), websocketpp::frame::opcode::text);
    }

    double roundedQty(double qty) { return std::round(qty * 10) / 10; }

    uint64_t sendOrder(double price,
                       double qty,
                       bool buy,
                       std::string instrumentId,
                       std::string ordType = "limit",
                       std::string tdMode = "cross",
                       bool banAmend = true) {
        uint64_t ret4 = helper::get_current_timestamp_ns();
        std::string clientOrderId = std::to_string(ret4);
        std::string side = buy ? "buy" : "sell";
        if(instrumentId == "BTC-USDT-SWAP") {
            qty = std::round(qty / (okx::BTC_USDT_SWAP::btcPerpCtVal) * 1e6) / 1e6;
        } else if(instrumentId == "DOGE-USDT-SWAP") {
            qty = std::round(qty / (okx::DOGE_USDT_SWAP::dogePerpCtVal) * 1e6) / 1e6;
        }
        nlohmann::json place_order_payload = {{"id", clientOrderId},
                                              {"op", "order"},
                                              {"args",
                                               {{{"instId", instrumentId},
                                                 {"tdMode", tdMode},
                                                 {"side", side},
                                                 {"ordType", ordType},
                                                 {"sz", qty},
                                                 {"banAmend", banAmend},
                                                 {"clOrdId", clientOrderId}}}}};

        if(ordType == "limit" || ordType == "post_only") {
            place_order_payload["args"][0]["px"] = price;
        }

        std::string payload_str = place_order_payload.dump();
        LoggerSingleton::get().plain().ws_request("new order payload: ", payload_str);

        try {
            routing_client.send(routing_hdl, payload_str, websocketpp::frame::opcode::text);
        } catch(const websocketpp::exception& e) {
            LoggerSingleton::get().infra().error("websocket send error: ", e.what());
            return 0;
        }
        return ret4;
    }

    uint64_t sendCancelOrder(uint64_t clOrdId, std::string instrumentId) {
        uint64_t ret = helper::get_current_timestamp_ns();
        std::string clientOrderId = std::to_string(ret);
        json cancel_order_payload = {{"id", clientOrderId},
                                     {"op", "cancel-order"},
                                     {"args", {{{"instId", instrumentId}, {"clOrdId", clOrdId}}}}};
        std::string payload_str = cancel_order_payload.dump();
        try {
            LoggerSingleton::get().plain().ws_request("cancel order payload: ", payload_str);
            routing_client.send(routing_hdl, std::move(payload_str), websocketpp::frame::opcode::text);
        } catch(const websocketpp::exception& e) {
            LoggerSingleton::get().infra().error("websocket send error", e.what());
            return 0;
        }
        return ret;
    }

    uint64_t modifyOrder(long long clOrdId, double newQty, double newPrice, std::string instrumentId) {
        uint64_t ret4 = helper::get_current_timestamp_ns();
        modify_order_payload.SetObject();
        rapidjson::Document::AllocatorType& allocator = modify_order_payload.GetAllocator();
        std::string clientOrderId = std::to_string(ret4);
        if(instrumentId == "BTC-USDT-SWAP")
            newQty = std::round(newQty / (okx::BTC_USDT_SWAP::btcPerpCtVal) * 1e6) / 1e6;
        else if(instrumentId == "DOGE-USDT-SWAP")
            newQty = std::round(newQty / (okx::DOGE_USDT_SWAP::dogePerpCtVal) * 1e6) / 1e6;
        // Add top-level keys
        modify_order_payload.AddMember("id", rapidjson::Value(clientOrderId.c_str(), allocator), allocator);
        modify_order_payload.AddMember("op", "amend-order", allocator);
        argsObject.SetObject();
        argsArray.SetArray();

        argsObject.AddMember("instId", rapidjson::Value(instrumentId.c_str(), allocator), allocator);
        argsObject.AddMember("clOrdId", rapidjson::Value(std::to_string(clOrdId).c_str(), allocator), allocator);
        argsObject.AddMember("newSz", rapidjson::Value(std::to_string(newQty).c_str(), allocator), allocator);
        argsObject.AddMember("newPx", rapidjson::Value(std::to_string(newPrice).c_str(), allocator), allocator);

        // Add the args object to the args array
        argsArray.PushBack(argsObject, allocator);

        // Add the args array to the main JSON payload
        modify_order_payload.AddMember("args", argsArray, allocator);

        buffer.Clear();
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        modify_order_payload.Accept(writer);
        LoggerSingleton::get().plain().ws_request("modify order payload: ", buffer.GetString());
        try {
            routing_client.send(routing_hdl, buffer.GetString(), websocketpp::frame::opcode::text);
        } catch(const websocketpp::exception& e) {
            LoggerSingleton::get().infra().error("websocket send error: ", e.what());
            return 0;
        }
        return ret4;
    }

    bool send_heartbeat() {
        try {
            LOG_INFRA_DEBUG("okx trades channel heartbeat: ping");
            routing_client.send(routing_hdl, "ping", websocketpp::frame::opcode::text);
            return true;
        } catch(const websocketpp::exception& e) {
            LoggerSingleton::get().infra().error("action=heartbeat exchage=okx stream=trades result=fail reason=",
                                                 e.what());
            return false;
        }
    }

    bool isWebsocketReady() const { return m_wsState; }

    void onOrderUpdateMessage(std::string message) {
        if(message == "pong") {
            LOG_INFRA_DEBUG("okx trades channel heartbeat: pong");
            LOG_INFRA_DEBUG("action=heartbeat exchange=okx stream=trades result=pass");
            return;
        }
        LoggerSingleton::get().plain().ws_broadcast("okx order update: ", message);
        json parsedMessage = json::parse(message);
        if(parsedMessage.contains("event") && parsedMessage["event"] == "login") {
            if(parsedMessage.contains("code")) {
                if(parsedMessage["code"] == "0") {
                    this->connId = parsedMessage["connId"].get<std::string>();
                    std::string OKX_FILLS_SUBSCRIBER_MESSAGE;
                    if(instrument == "DOGE-USDT-SWAP") {
                        OKX_FILLS_SUBSCRIBER_MESSAGE = requests::getOkxFillsSubscribeMessage("SWAP", "DOGE-USDT");
                    } else if(instrument == "BTC-USDT-SWAP") {
                        OKX_FILLS_SUBSCRIBER_MESSAGE = requests::getOkxFillsSubscribeMessage("SWAP", "BTC-USDT");
                    } else {
                        LoggerSingleton::get().infra().warning("need to add support for this instrument to get fill");
                        return;
                    }
                    routing_client.send(
                        routing_hdl, std::move(OKX_FILLS_SUBSCRIBER_MESSAGE), websocketpp::frame::opcode::text);
                } else {
                    LoggerSingleton::get().infra().error("login fail");
                    return;
                }
            }
        }
        if(parsedMessage.contains("event") && parsedMessage["event"] == "subscribe") {
            this->m_wsState = true;
        }
        if(orderUpdateCallback) {
            orderUpdateCallback(message);
        }
    }

private:
    std::string uri;
    std::string proxy_uri;
    const uint32_t retry_limit;
    uint32_t reconnect_attempt = 0;

    std::string connId = "";
    bool m_wsState = false;
    connection_hdl routing_hdl;
    client_tls routing_client;

    OrderUpdateCallback orderUpdateCallback;

    const std::string apiKey = "";
    const std::string secretKey = "";
    const std::string passphrase = "";

    const std::string instrument = "";

    rapidjson::Document place_order_payload;
    rapidjson::Document modify_order_payload;
    rapidjson::StringBuffer buffer;
    rapidjson::Value argsArray;
    rapidjson::Value argsObject;
    static constexpr const char* OP_ORDER = "order";
    static constexpr const char* INST_ID_KEY = "instId";
    static constexpr const char* TD_MODE_KEY = "tdMode";
    static constexpr const char* SIDE_KEY = "side";
    static constexpr const char* ORD_TYPE_KEY = "ordType";
    static constexpr const char* QTY_KEY = "sz";
    static constexpr const char* BAN_AMEND_KEY = "banAmend";
    static constexpr const char* CLIENT_ORDER_KEY = "clOrdId";
    static constexpr const char* BUY_STR = "buy";
    static constexpr const char* SELL_STR = "sell";
};
