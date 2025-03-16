#pragma once
#include "../utils/logger.hpp"
#include "../utils/requests.hpp"
#include "book.hpp"
#include "websocket.hpp"
#include <queue>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

class BinanceWebSocketClient : public WebSocketClient<BinanceWebSocketClient> {
public:
    using MarketDataUpdateCallback = std::function<void()>;
    using WebSocketStatusUpdateCallback = std::function<void(bool)>;
    using PoolAllocator = rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator>;

    explicit BinanceWebSocketClient(const bool trading_mode,
                                    const uint32_t retry_limit,
                                    const std::string& uri,
                                    const std::string& proxy_uri,
                                    const std::string instrument)
        : WebSocketClient(retry_limit, uri, proxy_uri, true)
        , m_binanceBook(instrument)
        , m_instrument(instrument)
        , trading_mode(trading_mode) {
        document = rapidjson::Document(&allocator);
    }

    void setMarketDataUpdateCallback(MarketDataUpdateCallback callback) {
        marketDataUpdateCallback = std::move(callback);
    }

    void setWebSocketStatusUpdateCallback(WebSocketStatusUpdateCallback callback) {
        websocketStatusUpdateCallback = std::move(callback);
    }
    void onClose(websocketpp::connection_hdl hdl, std::string message) {
        LoggerSingleton::get().infra().error("binance md channel closed");
        if(message == "disconnect") {
            if(websocketStatusUpdateCallback) {
                websocketStatusUpdateCallback(false);
            }
        } else if(message == "connection_end") {
            if(websocketStatusUpdateCallback) {
                websocketStatusUpdateCallback(true);
            }
        }
    }

    static inline double fastStrtod(const char* str) {
        double value;
        std::from_chars(str, str + strlen(str), value);
        return value;
    }

    static inline int getType(const rapidjson::Value& value) {
        if(value.IsInt()) {
            return value.GetInt();
        } else if(value.IsString()) {
            return std::strtol(value.GetString(), nullptr, 10);
        }
        return -1; // Invalid type
    }

    inline void parseMessage(const std::string& marketData) {
        document.Parse<rapidjson::kParseFullPrecisionFlag>(marketData.c_str());

        if(document.HasParseError()) {
            LoggerSingleton::get().infra().error("RapidJSON parse error: ", document.GetParseError());
            return;
        }

        if(!document.IsObject()) {
            LoggerSingleton::get().infra().error("Expected top-level JSON object");
            return;
        }

        if(document.HasMember("T") && document["T"].IsUint64()) {
            m_binanceBook.m_timestamp = document["T"].GetUint64() * 1000000ULL;
        } else {
            LoggerSingleton::get().infra().error("No 'T' (timestamp) in data");
        }

        // Extract best bid price from "b"
        if(document.HasMember("b") && document["b"].IsString()) {
            double bestBid = fastStrtod(document["b"].GetString());
            m_binanceBook.setBestBid(bestBid);
        }

        // Extract best ask price from "a"
        if(document.HasMember("a") && document["a"].IsString()) {
            double bestAsk = fastStrtod(document["a"].GetString());
            m_binanceBook.setBestAsk(bestAsk);
        }
    }

    inline void parseMockMessage(std::string& marketData) {
        rapidjson::Document document;
        document.Parse<rapidjson::kParseFullPrecisionFlag>(marketData.c_str());

        if(document.HasParseError() || !document.IsObject()) {
            return; // Handle parsing error gracefully
        }

        // Extract and update timestamp (E field), multiply by 10^6
        if(document.HasMember("E") && document["E"].IsUint64()) {
            m_binanceBook.m_timestamp = document["E"].GetUint64() * 1000000ULL;
        }

        // Extract bids and asks
        if(document.HasMember("b") && document["b"].IsArray() && document.HasMember("a") && document["a"].IsArray()) {

            const auto& bids = document["b"];
            const auto& asks = document["a"];

            if(!bids.Empty() && !asks.Empty()) {
                // Extract the best bid (highest bid price)
                const char* bestBidPriceStr = bids[0][0].GetString();
                const char* bestBidQtyStr = bids[0][1].GetString();
                double bestBidPrice = fastStrtod(bestBidPriceStr);
                double bestBidQty = fastStrtod(bestBidQtyStr);

                // Extract the best ask (lowest ask price)
                const char* bestAskPriceStr = asks[0][0].GetString();
                const char* bestAskQtyStr = asks[0][1].GetString();
                double bestAskPrice = fastStrtod(bestAskPriceStr);
                double bestAskQty = fastStrtod(bestAskQtyStr);
                m_binanceBook.setBestBid(bestBidPrice);
                m_binanceBook.setBestAsk(bestAskPrice);
            }
        }
    }

    void onMessage(websocketpp::connection_hdl hdl, client_non_tls::message_ptr msg) {
        // Parse the message payload into MarketData
        std::string message = msg->get_payload();
        LOG_INFRA_DEBUG("binance md payload: ", message);
        cnt += 1;
        if(cnt <= 2) return;
        double old_best_bid = m_binanceBook.getBestBid();
        double old_best_ask = m_binanceBook.getBestAsk();
        if(trading_mode)
            parseMessage(message);
        else
            parseMockMessage(message);
        this->m_bookWarmedUp = true;
        if(old_best_ask == m_binanceBook.getBestAsk() && old_best_bid == m_binanceBook.getBestBid()) {
            return;
        }
        if(marketDataUpdateCallback) {
            marketDataUpdateCallback();
        }
    }

    void onOpen(websocketpp::connection_hdl hdl) {
        websocketpp::lib::error_code ec;
        LoggerSingleton::get().infra().info("binance websocket connection opened");
        if(trading_mode) {
            if(ec) {
                LoggerSingleton::get().infra().error("error sending binance subscribe message: ", ec.message());
            }
        } else {
            m_instrument = mapping::getMockInstrument(m_instrument);
            std::string md_subscribe = requests::getBinanceDirectStream(m_instrument);
            ws_client->send(hdl, md_subscribe, websocketpp::frame::opcode::text);
            if(ec) {
                LoggerSingleton::get().infra().error("error sending binance subscribe message: ", ec.message());
            }
        }
    }

    [[nodiscard]]
    bool isBookReady() const noexcept {
        return m_bookWarmedUp;
    }

    const Book& getBook() const { return m_binanceBook; }

protected:
    Book m_binanceBook;

private:
    MarketDataUpdateCallback marketDataUpdateCallback;
    uint64_t m_timestamp;
    int cnt = 0;
    rapidjson::Document document;
    PoolAllocator allocator;
    WebSocketStatusUpdateCallback websocketStatusUpdateCallback;
    bool m_bookWarmedUp = false;
    std::string m_instrument;
    bool trading_mode = false;
};
