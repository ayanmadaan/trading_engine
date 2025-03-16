#pragma once
#include <sstream>
#include <string>
#include "instrumentmappings.hpp"
namespace requests {
    
    std::string getBinanceInit() {
        std::ostringstream message;
        message << "[13, \"YourOrganization\", \"YourApp\", \"1.0.0\", \"YourApp_Instance\"]";
        return message.str();
    }

    std::string getBinanceTopOfBookSubscribeCommand(std::string instrument) {
        mapping::InstrumentInfo binanceInstrument = mapping::getInstrumentInfo(instrument);
        std::ostringstream message;
        message << "[11, " << binanceInstrument.instrument << ", {"
                << "\"depthTopic\": false, "
                << "\"tradesTopic\": false, "
                << "\"topOfBookTopic\": true, "
                << "\"indexPriceTopic\": false, "
                << "\"markPriceTopic\": false, "
                << "\"fundingRateTopic\": false, "
                << "\"topOfBookCoalescing\": false"
                << "}]";
        
        return message.str();
    }

    std::string getBinanceBookSubscribeCommand(std::string instrument) {
        mapping::InstrumentInfo binanceInstrument = mapping::getInstrumentInfo(instrument);
        std::ostringstream message;
        message << "[11, " << binanceInstrument.instrument << ", {"
                << "\"depthTopic\": true, "
                << "\"tradesTopic\": false, "
                << "\"topOfBookTopic\": false, "
                << "\"indexPriceTopic\": false, "
                << "\"markPriceTopic\": false, "
                << "\"fundingRateTopic\": false, "
                << "\"topOfBookCoalescing\": false"
                << "}]";
        return message.str();

    }

    std::string getBinanceDirectStream(std::string instrument) {
        std::ostringstream message;
        message << "{\"method\": \"SUBSCRIBE\", \"params\": [\"" 
                << instrument << "@depth20@100ms\"], \"id\": 1}";
        return message.str();
    }

    std::string getOkxTopOfBookSubscribeMessage(std::string instrument) {
        std::ostringstream message;
        message << R"({
                "op": "subscribe",
                "args": [
                    {
                        "channel": "bbo-tbt",
                        "instId": ")"
                << instrument << R"("
                    }
                ]
            })";
        return message.str();
    }

    std::string getOkxTopFiveLevelBookSubscribeMessage(std::string instrument) {
        std::ostringstream message;
        message << R"({
                "op": "subscribe",
                "args": [
                    {
                        "channel": "books5",
                        "instId": ")"
                << instrument << R"("
                    }
                ]
            })";
        return message.str();
    }

    std::string getOkxBookSubscribeMessage(std::string instrument) {
        std::ostringstream message;
        message << R"({
                "op": "subscribe",
                "args": [
                    {
                        "channel": "books",
                        "instId": ")"
                << instrument << R"("
                    }
                ]
            })";
        return message.str();
    }

    std::string getOkxBook50SubscribeMessage(std::string instrument) {
        std::ostringstream message;
        message << R"({
                "op": "subscribe",
                "args": [
                    {
                        "channel": "books50-l2-tbt",
                        "instId": ")"
                << instrument << R"("
                    }
                ]
            })";
        return message.str();
    }

    std::string getOkxBook400FastSubscribeMessage(std::string instrument) {
        std::ostringstream message;
        message << R"({
                "op": "subscribe",
                "args": [
                    {
                        "channel": "books-l2-tbt",
                        "instId": ")"
                << instrument << R"("
                    }
                ]
            })";
        return message.str();
    }

    std::string getOkxFillsSubscribeMessage(std::string instType, std::string instFamily) {
        std::ostringstream message;
        message << R"({
                "op": "subscribe",
                "args": [
                    {
                        "channel": "orders",
                        "instType": ")"
                << instType << R"(",
                        "instFamily": ")"
                << instFamily << R"("
                    }
                ]
            })";
        return message.str();
    }

    std::string getByBitFillsSubscribeMessage() {
        std::ostringstream message;
        message << R"({
                "op": "subscribe",
                "args": [
                    "order"
                ]
            })";
        return message.str();
    }

    std::string getByBitExecutionSubscribeMessage() {
        std::ostringstream message;
        message << R"({
                "op": "subscribe",
                "args": [
                    "execution"
                ]
            })";
        return message.str();
    }

    std::string getByBitOrderBookMessage(uint16_t depth, std::string instFamily) {
        std::ostringstream message;
        message << R"({
                "op": "subscribe",
                "args": [
                    "orderbook.)"
                << std::to_string(depth) << R"(.)" << instFamily << R"("
                ]
            })";

        return message.str();
    }
} // namespace requests
