// OKXWebSocketClient.h
#pragma once
#include "../utils/helper.hpp"
#include "../utils/instrumentmappings.hpp"
#include "../utils/logger.hpp"
#include "../utils/requests.hpp"
#include "book.hpp"
#include "websocket.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <immintrin.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <unordered_map>
#include <x86intrin.h>

class OKXWebSocketClient : public WebSocketClient<OKXWebSocketClient> {
public:
    using MarketDataUpdateCallback = std::function<void()>;
    using WebSocketStatusUpdateCallback = std::function<void(bool)>;
    using PoolAllocator = rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator>;

    explicit OKXWebSocketClient(const std::string& uri,
                                const std::string& proxy_uri,
                                const std::string instrument,
                                const uint32_t retry_limit,
                                const std::string apiKey,
                                const std::string apiSecret,
                                const std::string apiPassphrase)
        : WebSocketClient(retry_limit, uri, proxy_uri)
        , m_instrument(instrument)
        , m_okxBook(instrument)
        , apiKey(apiKey)
        , apiSecret(apiSecret)
        , apiPassphrase(apiPassphrase) {
        document = rapidjson::Document(&allocator);
    }

    void setMarketDataUpdateCallback(MarketDataUpdateCallback callback) {
        marketDataUpdateCallback = std::move(callback);
    }

    void setWebSocketStatusUpdateCallback(WebSocketStatusUpdateCallback callback) {
        webSocketStatusUpdateCallback = std::move(callback);
    }

    void onClose(websocketpp::connection_hdl hdl, std::string message) {
        LoggerSingleton::get().infra().error("okx md channel closed");
        m_bookWarmedUp = false;
        cnt = 0;
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
            LOG_INFRA_DEBUG("okx md channel heartbeat: ping");
            ws_client->send(current_hdl, "ping", websocketpp::frame::opcode::text);
        } catch(const websocketpp::exception& e) {
            LoggerSingleton::get().infra().error("action=heartbeat exchage=okx stream=md result=fail reason=",
                                                 e.what());
            if(webSocketStatusUpdateCallback) {
                webSocketStatusUpdateCallback(false);
            }
        }
    }

    static inline double fastStrtod(const char* str) {
        double value;
        std::from_chars(str, str + strlen(str), value);
        return value;
    }

    void authenticate() {
        std::string timestamp = helper::get_current_timestamp();
        std::string method = "GET";
        std::string request_path = "/users/self/verify/";
        std::string body = "";
        std::string string_to_sign = helper::create_string_to_sign(timestamp, method, request_path, body);
        std::string signature = helper::generate_signature(apiSecret, timestamp);
        std::string login_payload = "{\"op\": \"login\", "
                                    "\"args\": [{"
                                    "\"apiKey\": \"" +
                                    apiKey +
                                    "\", "
                                    "\"passphrase\": \"" +
                                    apiPassphrase +
                                    "\", "
                                    "\"sign\": \"" +
                                    signature +
                                    "\", "
                                    "\"timestamp\": \"" +
                                    timestamp + "\"}]}";
        LoggerSingleton::get().plain().ws_request("login payload: ", login_payload);
    }

    inline void parseMessage(std::string& marketData) {
        document.Parse<rapidjson::kParseNumbersAsStringsFlag | rapidjson::kParseFullPrecisionFlag>(marketData.c_str());

        const auto& data = document["data"][0];
        m_okxBook.bidSide.size = 0;
        m_okxBook.askSide.size = 0;

        // Process asks
        const auto& asks = data["asks"];
        for(const auto& level : asks.GetArray()) {
            // if (level.IsArray() && level.Size() >= 2) {
            double price = fastStrtod(level[0].GetString());
            double qty = fastStrtod(level[1].GetString());
            m_okxBook.askSide.insert(price, qty); // Using PriceLevelArray's insert method
            //}
        }

        // Process bids
        const auto& bids = data["bids"];
        for(const auto& level : bids.GetArray()) {
            // if (level.IsArray() && level.Size() >= 2) {
            double price = fastStrtod(level[0].GetString());
            double qty = fastStrtod(level[1].GetString());
            m_okxBook.bidSide.insert(price, qty); // Using PriceLevelArray's insert method
            //}
        }
        // static_cast<Derived*>(this)->onOkxMarketDataUpdate(m_okxBook);
    }

    inline void parseBookMessage(std::string& marketData) {
        document.Parse<rapidjson::kParseFullPrecisionFlag>(marketData.c_str());
        uint64_t timestamp = std::stoull(document["data"][0]["ts"].GetString());
        m_okxBook.m_timestamp = timestamp * milliToNano;
        const auto& asks = document["data"][0]["asks"];
        for(const auto& level : asks.GetArray()) {
            double price = fastStrtod(level[0].GetString());
            double qty = fastStrtod(level[1].GetString());
            m_okxBook.setBestAsk(price);
        }

        const auto& bids = document["data"][0]["bids"];
        for(const auto& level : bids.GetArray()) {
            double price = fastStrtod(level[0].GetString());
            double qty = fastStrtod(level[1].GetString());
            m_okxBook.setBestBid(price);
        }
        // static_cast<Derived*>(this)->onOkxMarketDataUpdate(m_okxBook);
    }

    void onMessage(websocketpp::connection_hdl hdl, client_tls::message_ptr msg) {
        // Parse the message payload into MarketData
        cnt += 1;
        std::string message = msg->get_payload();
        LOG_INFRA_DEBUG("okx md payload: ", message);
        if(cnt == 1) {
            return;
        }
        if(message == "pong") {
            LOG_INFRA_DEBUG("okx md channel heartbeat: pong");
            LOG_INFRA_DEBUG("action=heartbeat exchange=okx stream=md result=pass");
            return;
        }
        double old_best_bid = m_okxBook.getBestBid();
        double old_best_ask = m_okxBook.getBestAsk();
        parseBookMessage(message);
        this->m_bookWarmedUp = true;
        if(old_best_bid == m_okxBook.getBestBid() && old_best_ask == m_okxBook.getBestAsk()) {
            return;
        }
        if(marketDataUpdateCallback) {
            marketDataUpdateCallback();
        }
        // if (cnt%100 == 0) {
        //     ws_client->close(current_hdl, websocketpp::close::status::normal, "Testing closure");
        //     LoggerSingleton::get().infra().warning("closed okx md channel for testing purposes");
        // }
    }

    void onOpen(websocketpp::connection_hdl hdl) {
        websocketpp::lib::error_code ec;
        LoggerSingleton::get().infra().info("okx websocket connection opened");
        flag = true;
        if(!flag) {
            std::string timestamp = helper::get_current_timestamp();
            std::string method = "GET";
            std::string request_path = "/users/self/verify/";
            std::string body = "";
            std::string string_to_sign = helper::create_string_to_sign(timestamp, method, request_path, body);
            std::string signature = helper::generate_signature(apiSecret, timestamp);
            std::string login_payload = "{\"op\": \"login\", "
                                        "\"args\": [{"
                                        "\"apiKey\": \"" +
                                        apiKey +
                                        "\", "
                                        "\"passphrase\": \"" +
                                        apiPassphrase +
                                        "\", "
                                        "\"sign\": \"" +
                                        signature +
                                        "\", "
                                        "\"timestamp\": \"" +
                                        timestamp + "\"}]}";
            LoggerSingleton::get().plain().ws_request("login payload: ", login_payload);
            this->ws_client->send(hdl, std::move(login_payload), websocketpp::frame::opcode::text);
        }
        mapping::InstrumentInfo exchangeInst = mapping::getInstrumentInfo(m_instrument);
        std::string OKX_SUBSCRIBE_MESSAGE = requests::getOkxTopOfBookSubscribeMessage(exchangeInst.instrument);
        this->ws_client->send(hdl, OKX_SUBSCRIBE_MESSAGE, websocketpp::frame::opcode::text, ec);
        if(ec) {
            LoggerSingleton::get().infra().error("error sending okx subscribe message: ", ec.message());
        }
    }

    [[nodiscard]]
    bool isBookReady() const noexcept {
        return this->m_bookWarmedUp;
    }

    const Book& getBook() const { return m_okxBook; }

    static const uint32_t milliToNano = 1000000;

protected:
    Book m_okxBook;

private:
    MarketDataUpdateCallback marketDataUpdateCallback;
    WebSocketStatusUpdateCallback webSocketStatusUpdateCallback;
    uint64_t m_timestamp;
    rapidjson::Document document;
    PoolAllocator allocator;
    int cnt = 0;
    bool flag = false;
    bool m_bookWarmedUp = false;
    const std::string m_instrument;
    const std::string apiKey;
    const std::string apiSecret;
    const std::string apiPassphrase;
};
