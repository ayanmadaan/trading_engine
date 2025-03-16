#pragma once
#include "../lib/json.hpp"
#include "../utils/instrumentmappings.hpp"
#include "../utils/logger.hpp"
#include "../utils/requests.hpp"
#include "book.hpp"
#include "websocket.hpp"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <string.h>

using nlohmann::json;

class ByBitWebSocketClient : public WebSocketClient<ByBitWebSocketClient> {
public:
    using MarketDataUpdateCallback = std::function<void()>;
    using WebSocketStatusUpdateCallback = std::function<void(bool)>;
    using PoolAllocator = rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator>;

    explicit ByBitWebSocketClient(const std::string& uri,
                                  const std::string& proxy_uri,
                                  const std::string instrument,
                                  const uint32_t retry_limit,
                                  const std::string apiKey,
                                  const std::string apiSecret)
        : WebSocketClient(retry_limit, uri, proxy_uri)
        , m_instrument(instrument)
        , m_byBitBook(instrument)
        , apiKey(apiKey)
        , apiSecret(apiSecret) {}

    void setMarketDataUpdateCallback(MarketDataUpdateCallback callback) {
        marketDataUpdateCallback = std::move(callback);
    }

    void setWebSocketStatusUpdateCallback(WebSocketStatusUpdateCallback callback) {
        webSocketStatusUpdateCallback = std::move(callback);
    }

    void onClose(websocketpp::connection_hdl hdl, std::string message) {
        LoggerSingleton::get().infra().error("bybit md channel closed");
        m_bookWarmedUp = false;
        if(message == "disconnect") {
            if(webSocketStatusUpdateCallback) {
                webSocketStatusUpdateCallback(false);
            }
            return;
        }
        if(message == "connection_end") {
            if(webSocketStatusUpdateCallback) {
                webSocketStatusUpdateCallback(true);
            }
        }
    }

    void send_heartbeat() {
        try {
            nlohmann::json ping_msg = {{"op", "ping"}};
            LOG_INFRA_DEBUG("bybit md channel heartbeat: ping");
            ws_client->send(current_hdl, ping_msg.dump(), websocketpp::frame::opcode::text);
        } catch(const std::exception& e) {
            LoggerSingleton::get().infra().error("action=heartbeat exchage=bybit stream=md result=fail reason=",
                                                 e.what());
            if(webSocketStatusUpdateCallback) {
                webSocketStatusUpdateCallback(false);
            }
        }
    }

    inline void parseMessage(std::string& message) {
        json parsedJson = json::parse(message);
        if(parsedJson.contains("op") && parsedJson["op"] == "ping") {
            LOG_INFRA_DEBUG("bybit md channel heartbeat: pong");
            LOG_INFRA_DEBUG("action=heartbeat exchange=bybit stream=md result=pass");
            return;
        }
        LOG_INFRA_DEBUG("bybit md payload: ", message);
        if(parsedJson.contains("ts")) {
            long long ts = parsedJson["ts"];
            m_byBitBook.m_timestamp = ts * milliToNano;
        }
        for(const auto& bid : parsedJson["data"]["b"]) {
            m_byBitBook.setBestBid(std::stod(bid[0].get<std::string>()));
        }
        for(const auto& ask : parsedJson["data"]["a"]) {
            m_byBitBook.setBestAsk(std::stod(ask[0].get<std::string>()));
        }
    }

    void onMessage(websocketpp::connection_hdl hdl, client_tls::message_ptr msg) {
        // cnt += 1;
        std::string message = msg->get_payload();
        double old_best_bid = m_byBitBook.getBestBid();
        double old_best_ask = m_byBitBook.getBestAsk();
        parseMessage(message);
        this->m_bookWarmedUp = true;
        if(old_best_bid == m_byBitBook.getBestBid() && old_best_ask == m_byBitBook.getBestAsk()) {
            return;
        }
        if(marketDataUpdateCallback) {
            marketDataUpdateCallback();
        }

        // if (cnt%100 == 0) {
        //     ws_client->close(current_hdl, websocketpp::close::status::normal, "Testing closure");
        //     LoggerSingleton::get().infra().warning("closed bybit md channel for testing purposes");
        // }
    }

    void authenticate() {
        websocketpp::lib::error_code ec;
        long long timestamp = helper::get_current_timestamp_ms();
        uint64_t expires = (timestamp + 1000);
        std::string message = "GET/realtime" + std::to_string(expires);
        std::string signature = helper::generate_signature_bybit(apiSecret, message);
        nlohmann::json auth_payload = {{"op", "auth"}, {"args", {apiKey, expires, signature}}};
    }

    void onOpen(websocketpp::connection_hdl hdl) {
        websocketpp::lib::error_code ec;
        LoggerSingleton::get().infra().info("bybit websocket connection opened");
        mapping::InstrumentInfo exchangeInst = mapping::getInstrumentInfo(m_instrument);
        std::string subscribeMessage = requests::getByBitOrderBookMessage(1, exchangeInst.instrument);
        ws_client->send(hdl, subscribeMessage, websocketpp::frame::opcode::text, ec);
    }

    [[nodiscard]]
    bool isBookReady() const noexcept {
        return m_bookWarmedUp;
    }

    const Book& getBook() const { return m_byBitBook; }

    static const uint32_t milliToNano = 1000000;

protected:
    Book m_byBitBook;

private:
    MarketDataUpdateCallback marketDataUpdateCallback;
    WebSocketStatusUpdateCallback webSocketStatusUpdateCallback;
    bool m_bookWarmedUp = false;
    const std::string apiKey;
    const std::string apiSecret;
    const std::string m_instrument;
    int cnt = 0;
};
