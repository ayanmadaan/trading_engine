#pragma once

#include "../src/Side.h"
#include "../utils/helper.hpp"
#include "../utils/logger.hpp"
#include "okxclient.hpp"
#include "okxordersrouting.hpp"
#include "okxpositionmanager.hpp"
#include "orderhandler.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

class OkxOrderManager {
public:
    using OrderStatusUpdateCallback = std::function<void(OrderHandler&)>;
    using WebSocketStatusUpdateCallback = std::function<void(bool)>;

    explicit OkxOrderManager(const bool trading_mode,
                             const uint32_t track_order_cnt,
                             const uint32_t retry_limit,
                             const std::string& proxy_uri,
                             const std::string api_key,
                             const std::string api_secret,
                             const std::string api_passphrase,
                             const std::string instrument,
                             OkxPositionManager& manager)
        : m_trackOrderCnt(track_order_cnt)
        , m_positionManager(manager)
        , m_instrument(instrument)
        , retry_limit(retry_limit)
        , m_client(trading_mode, api_key, api_secret, api_passphrase) {
        okxOrderRouter =
            std::make_unique<OkxOrderRouter>(trading_mode,
                                             proxy_uri,
                                             retry_limit,
                                             api_key,
                                             api_secret,
                                             api_passphrase,
                                             m_instrument,
                                             [this](std::string message) { this->updateOrderStatus(message); });
    }
    void run() { okxOrderRouter->setupRoutingConnection(); }

    void stop() { okxOrderRouter->stop(); }

    bool cancelAll() { return m_client.cancelAll(); }

    void setOrderStatusUpdateCallback(OrderStatusUpdateCallback callback) {
        std::lock_guard<std::mutex> lock(m_mutex);
        orderStatusUpdateCallback = std::move(callback);
    }

    std::pair<bool, double> getRealisedPnlOfCurrentDay() const { return m_client.getRealisedPnlOfCurrentDay(); }

    std::pair<bool, double> getUnrealisedPnl() const { return m_client.getUnrealisedPnl(); }

    std::pair<bool, double> getRealisedPnlBetweenTimeInterval(uint64_t startTime, uint64_t endTime) const {
        return m_client.getRealisedPnlBetweenTimeInterval(startTime, endTime);
    }

    void setWebsocketHealthCallback(WebSocketStatusUpdateCallback callback) {
        std::lock_guard<std::mutex> lock(m_mutex);
        websocketStatusUpdateCallback = std::move(callback);
    }

    void updateOrderStatus(std::string message) {
        if(message == "disconnect") {
            if(websocketStatusUpdateCallback) {
                websocketStatusUpdateCallback(false);
            }
            return;
        }
        if(message == "connection_end") {
            if(websocketStatusUpdateCallback) {
                websocketStatusUpdateCallback(true);
            }
            return;
        }
        getUpdatedOrderStatus(message);
    }

    void maintainOrderLimit() {
        std::lock_guard<std::mutex> lock(m_mutex);
        while(cancelQueue.size() > m_trackOrderCnt) {
            uint64_t clOrderId = cancelQueue.front();
            auto it = orderMap.find(clOrderId);
            if(it != orderMap.end()) {
                orderMap.erase(clOrderId);
            }
            cancelQueue.pop();
        }

        while(rejectedQueue.size() > m_trackOrderCnt) {
            uint64_t clOrderId = rejectedQueue.front();
            auto it = orderMap.find(clOrderId);
            if(it != orderMap.end()) {
                orderMap.erase(clOrderId);
            }
            rejectedQueue.pop();
        }

        while(filledQueue.size() > m_trackOrderCnt) {
            uint64_t clOrderId = filledQueue.front();
            auto it = orderMap.find(clOrderId);
            if(it != orderMap.end()) {
                orderMap.erase(clOrderId);
            }
            filledQueue.pop();
        }
    }

    // Retrieves the current status of an order
    [[nodiscard]]
    OrderStatus getOrderStatus(const uint64_t orderId) const noexcept {
        std::lock_guard<std::mutex> lock(m_mutableMutex);
        auto it = this->orderMap.find(orderId);
        if(it == this->orderMap.end()) {
            return OrderStatus::PENDING;
        } else {
            return it->second.get()->m_status;
        }
    }

    [[nodiscard]]
    std::vector<std::shared_ptr<OrderHandler>> getOrdersByStatus(OrderStatus status) const noexcept {
        std::vector<std::shared_ptr<OrderHandler>> res;
        std::lock_guard<std::mutex> lock(m_mutableMutex);
        for(const auto& [key, orderPtr] : this->orderMap) {
            if(orderPtr->m_status == status) {
                res.push_back(orderPtr);
            }
        }
        return res;
    }

    uint64_t placeOrder(const std::string& instrumentId,
                        double price,
                        double qty,
                        bool buy,
                        const std::string& ordType = "limit",
                        const std::string& tdMode = "cross",
                        bool banAmend = true) {
        // std::lock_guard<std::mutex> lock(m_mutex);
        std::shared_ptr<OrderHandler> orderHandler = createOrderHandler(instrumentId);
        orderHandler->m_newOrderOnOmsTS = helper::get_current_timestamp_ns();
        mapping::InstrumentInfo inst = mapping::getInstrumentInfo(instrumentId);
        if(!isWebSocketReady()) {
            orderHandler->m_status = OrderStatus::REJECTED;
            orderHandler->m_reason = RejectReason::WS_FAILURE;
            if(orderStatusUpdateCallback) {
                orderStatusUpdateCallback(*orderHandler);
            }
            return 0;
        }
        uint64_t clientOrderId = okxOrderRouter->sendOrder(price, qty, buy, inst.instrument, ordType, tdMode, banAmend);
        if(clientOrderId != 0) {
            orderHandler->m_clientOrderId = clientOrderId;
            orderHandler->m_status = OrderStatus::PENDING;
            orderHandler->m_reason = RejectReason::NONE;
            orderHandler->m_side = buy;
            orderHandler->m_qtySubmitted = qty;
            orderHandler->m_priceSubmitted = price;
            orderMap.emplace(clientOrderId, std::move(orderHandler));
        } else {
            // Handle failure if necessary
            orderHandler->m_status = OrderStatus::REJECTED;
            orderHandler->m_reason = RejectReason::WS_FAILURE;
            if(orderStatusUpdateCallback) {
                orderStatusUpdateCallback(*orderHandler);
            }
            if(websocketStatusUpdateCallback) {
                websocketStatusUpdateCallback(false);
            }
        }
        return clientOrderId;
    }

    // Cancel an existing order by orderId
    uint64_t cancelOrder(uint64_t clientOrderId, const std::string& m_instrument) {
        // std::lock_guard<std::mutex> lock(m_mutex);
        auto orderHandlerIterator = orderMap.find(clientOrderId);
        if(orderHandlerIterator == orderMap.end()) {
            std::shared_ptr<OrderHandler> newOrderHandler = createOrderHandler(m_instrument);
            auto [iterator, inserted] = orderMap.emplace(clientOrderId, std::move(newOrderHandler));
            orderHandlerIterator = iterator;
        }
        auto& orderHandler = orderHandlerIterator->second;
        if(!isWebSocketReady()) {
            orderHandler->m_status = OrderStatus::REJECTED;
            orderHandler->m_reason = RejectReason::WS_FAILURE;
            if(orderStatusUpdateCallback) {
                orderStatusUpdateCallback(*orderHandler);
            }
            return clientOrderId;
        }
        orderHandler->m_cancelOrderOnOmsTS = helper::get_current_timestamp_ns();
        mapping::InstrumentInfo inst = mapping::getInstrumentInfo(m_instrument);
        uint64_t ret = okxOrderRouter->sendCancelOrder(clientOrderId, inst.instrument);
        if(ret == 0) {
            orderHandler->m_status = OrderStatus::REJECTED;
            orderHandler->m_reason = RejectReason::WS_FAILURE;
            if(orderStatusUpdateCallback) {
                orderStatusUpdateCallback(*orderHandler);
            }
            if(websocketStatusUpdateCallback) {
                websocketStatusUpdateCallback(false);
            }
        } else {
            orderHandler->m_clientOrderId = clientOrderId;
        }
        return ret;
    }

    void send_heartbeat() {
        bool status = okxOrderRouter->send_heartbeat();
        if((!status) && websocketStatusUpdateCallback) {
            websocketStatusUpdateCallback(false);
        }
    }

    // Modify an existing order
    uint64_t modifyOrder(uint64_t clientOrderId, double newPrice, double newQty, std::string m_instrument) {
        // std::lock_guard<std::mutex> lock(m_mutex);
        auto orderHandlerIterator = orderMap.find(clientOrderId);
        if(orderHandlerIterator == orderMap.end()) {
            std::shared_ptr<OrderHandler> newOrderHandler = createOrderHandler(m_instrument);
            auto [iterator, inserted] = orderMap.emplace(clientOrderId, std::move(newOrderHandler));
            orderHandlerIterator = iterator;
        }
        auto& orderHandler = orderHandlerIterator->second;
        if(!isWebSocketReady()) {
            orderHandler->m_status = OrderStatus::REJECTED;
            orderHandler->m_reason = RejectReason::WS_FAILURE;
            if(orderStatusUpdateCallback) {
                orderStatusUpdateCallback(*orderHandler);
            }
            return clientOrderId;
        }
        orderHandler->m_modifyOrderOnOmsTS = helper::get_current_timestamp_ns();
        orderHandler->m_qtySubmitted = newQty;
        mapping::InstrumentInfo inst = mapping::getInstrumentInfo(m_instrument);
        uint64_t ret = okxOrderRouter->modifyOrder(clientOrderId, newQty, newPrice, inst.instrument);
        if(ret == 0) {
            orderHandler->m_status = OrderStatus::REJECTED;
            orderHandler->m_reason = RejectReason::WS_FAILURE;
            if(orderStatusUpdateCallback) {
                orderStatusUpdateCallback(*orderHandler);
            }
            if(websocketStatusUpdateCallback) {
                websocketStatusUpdateCallback(false);
            }
        } else {
            orderHandler->m_clientOrderId = clientOrderId;
            orderHandler->m_priceSubmitted = newPrice;
        }
        return ret;
    }

    bool isWebSocketReady() const { return okxOrderRouter->isWebsocketReady(); }

    std::shared_ptr<OrderHandler> createOrderHandler(const std::string& m_instrument) {
        return std::make_shared<OrderHandler>(m_instrument);
    }

    OrderHandler* getOrderHandler(uint64_t clientOrderId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = orderMap.find(clientOrderId);
        if(it != orderMap.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    void getUpdatedOrderStatus(const std::string& message) {
        json parsedMessage = json::parse(message);
        if(parsedMessage.contains("id")) {
            if(parsedMessage.contains("code")) {
                if(parsedMessage["code"] == "1") {
                    /*Got a reject*/
                    if(parsedMessage.contains("data")) {
                        for(const auto& orderData : parsedMessage["data"]) {
                            std::string clOrdId = orderData["clOrdId"];
                            std::string error_code = orderData["sCode"];
                            uint64_t key = std::stoull(clOrdId);
                            auto iterator = this->orderMap.find(key);
                            if(iterator != orderMap.end()) {
                                auto order = iterator->second;
                                order->m_rejectionTS =
                                    std::stoull(parsedMessage["inTime"].get<std::string>()) * microToNano;
                                order->m_status = OrderStatus::REJECTED;
                                if(error_code == "50018") {
                                    order->m_reason = RejectReason::INSUFFICIENT_FUNDS;
                                    if(!order->m_orderHasBeenLive) {
                                        rejectedQueue.push(order->m_clientOrderId);
                                        maintainOrderLimit();
                                    }
                                } else if(error_code == "51008") {
                                    order->m_reason = RejectReason::INSUFFICIENT_FUNDS;
                                    if(!order->m_orderHasBeenLive) {
                                        rejectedQueue.push(order->m_clientOrderId);
                                        maintainOrderLimit();
                                    }
                                } else if(error_code == "51503") {
                                    order->m_reason = RejectReason::ORDER_DOES_NOT_EXIST_ON_EXCH_ORDERBOOK;
                                    rejectedQueue.push(order->m_clientOrderId);
                                    maintainOrderLimit();
                                } else if(error_code == "50011") {
                                    order->m_reason = RejectReason::THROTTLE_HIT;
                                    order->m_placeOrderNow = order->m_rejectionTS + (2000000000);
                                    if(!order->m_orderHasBeenLive) {
                                        rejectedQueue.push(order->m_clientOrderId);
                                        maintainOrderLimit();
                                    }
                                } else if(error_code == "51006") {
                                    order->m_reason = RejectReason::ORDER_PRICE_NOT_IN_RANGE;
                                    if(!order->m_orderHasBeenLive) {
                                        rejectedQueue.push(order->m_clientOrderId);
                                        maintainOrderLimit();
                                    }
                                } else if(error_code == "51400") {
                                    order->m_reason = RejectReason::ORDER_HAS_BEEN_FILLED_OR_CANCELLED;
                                    rejectedQueue.push(order->m_clientOrderId);
                                    maintainOrderLimit();
                                } else if(error_code == "51121") {
                                    order->m_reason = RejectReason::ORDER_SIZE_NOT_MULTIPLE_OF_LOT_SIZE;
                                    if(!order->m_orderHasBeenLive) {
                                        rejectedQueue.push(order->m_clientOrderId);
                                        maintainOrderLimit();
                                    }
                                } else if(error_code == "51503") {
                                    order->m_reason = RejectReason::ORDER_HAS_BEEN_FILLED_OR_CANCELLED;
                                    rejectedQueue.push(order->m_clientOrderId);
                                    maintainOrderLimit();
                                } else if(error_code == "50001") {
                                    order->m_reason = RejectReason::SERVICE_TEMPORARILY_UNAVAILABLE;
                                    if(!order->m_orderHasBeenLive) {
                                        rejectedQueue.push(order->m_clientOrderId);
                                        maintainOrderLimit();
                                    }
                                } else if(error_code == "50005") {
                                    order->m_reason = RejectReason::API_OFFLINE_OR_UNAVAILABLE;
                                    if(!order->m_orderHasBeenLive) {
                                        rejectedQueue.push(order->m_clientOrderId);
                                        maintainOrderLimit();
                                    }
                                } else if(error_code == "50007") {
                                    order->m_reason = RejectReason::ACCOUNT_BLOCKED;
                                    if(!order->m_orderHasBeenLive) {
                                        rejectedQueue.push(order->m_clientOrderId);
                                        maintainOrderLimit();
                                    }
                                } else if(error_code == "50013") {
                                    order->m_reason = RejectReason::EXCHANGE_BUSY;
                                    if(!order->m_orderHasBeenLive) {
                                        rejectedQueue.push(order->m_clientOrderId);
                                        maintainOrderLimit();
                                    }
                                } else if(error_code == "50033") {
                                    order->m_reason = RejectReason::INSTRUMENT_BLOCKED;
                                    if(!order->m_orderHasBeenLive) {
                                        rejectedQueue.push(order->m_clientOrderId);
                                        maintainOrderLimit();
                                    }
                                } else if(error_code == "50038") {
                                    order->m_reason = RejectReason::FEATURE_UNAVAILABLE_IN_DEMO;
                                    if(!order->m_orderHasBeenLive) {
                                        rejectedQueue.push(order->m_clientOrderId);
                                        maintainOrderLimit();
                                    }
                                } else if(error_code == "50052") {
                                    order->m_reason =
                                        RejectReason::CANNOT_TRADE_ON_CHOSEN_CRYPTO_DUE_TO_LOCAL_NEWS_AND_REGULATIONS;
                                    if(!order->m_orderHasBeenLive) {
                                        rejectedQueue.push(order->m_clientOrderId);
                                        maintainOrderLimit();
                                    }
                                } else if(error_code == "50101") {
                                    order->m_reason = RejectReason::API_KEY_DOES_NOT_MATCH_ENV;
                                    if(!order->m_orderHasBeenLive) {
                                        rejectedQueue.push(order->m_clientOrderId);
                                        maintainOrderLimit();
                                    }
                                } else {
                                    order->m_reason = RejectReason::UNKNOWN_ERROR;
                                    if(!order->m_orderHasBeenLive) {
                                        rejectedQueue.push(order->m_clientOrderId);
                                        maintainOrderLimit();
                                    }
                                }
                                if(orderStatusUpdateCallback) {
                                    orderStatusUpdateCallback(*order);
                                }
                            }
                        }
                    }
                } else if(parsedMessage["code"] == "0") {
                    if(parsedMessage.contains("op") && parsedMessage["op"] == "order") {
                        if(parsedMessage.contains("data")) {
                            for(const auto& orderData : parsedMessage["data"]) {
                                std::string clOrdId = orderData["clOrdId"];
                                uint64_t key = std::stoull(clOrdId);
                                auto iterator = this->orderMap.find(key);
                                if(iterator != orderMap.end()) {
                                    auto order = iterator->second;
                                    order->m_newOrderOnExchTS =
                                        std::stoull(parsedMessage["inTime"].get<std::string>()) * microToNano;
                                    order->m_newOrderConfirmationTS = helper::get_current_timestamp_ns();
                                }
                            }
                        }
                    } else if(parsedMessage.contains("op") && parsedMessage["op"] == "amend-order") {
                        if(parsedMessage.contains("data")) {
                            for(const auto& orderData : parsedMessage["data"]) {
                                std::string clOrdId = orderData["clOrdId"];
                                uint64_t key = std::stoull(clOrdId);
                                auto iterator = this->orderMap.find(key);
                                if(iterator != orderMap.end()) {
                                    auto order = iterator->second;
                                    order->m_modifyOrderOnExchTS =
                                        std::stoull(parsedMessage["inTime"].get<std::string>()) * microToNano;
                                    order->m_modifyOrderConfirmationTS = helper::get_current_timestamp_ns();
                                }
                            }
                        }
                    } else if(parsedMessage.contains("op") && parsedMessage["op"] == "cancel-order") {
                        if(parsedMessage.contains("data")) {
                            for(const auto& orderData : parsedMessage["data"]) {
                                std::string clOrdId = orderData["clOrdId"];
                                uint64_t key = std::stoull(clOrdId);
                                auto iterator = this->orderMap.find(key);
                                if(iterator != orderMap.end()) {
                                    auto order = iterator->second;
                                    order->m_cancelOrderOnExchTS =
                                        std::stoull(parsedMessage["inTime"].get<std::string>()) * microToNano;
                                    order->m_cancelOrderConfirmationTS = helper::get_current_timestamp_ns();
                                }
                            }
                        }
                    }
                }
            }
        } else if(parsedMessage.contains("arg")) {
            if(parsedMessage["arg"].contains("channel")) {
                for(const auto& orderData : parsedMessage["data"]) {
                    if(orderData.contains("clOrdId")) {
                        std::string clOrdId = orderData["clOrdId"];
                        std::string instId = orderData["instId"].get<std::string>();
                        double factor = 1.0;
                        if(m_instrument == "DOGE-USDT-SWAP") {
                            factor = (okx::DOGE_USDT_SWAP::dogePerpCtVal) * (okx::DOGE_USDT_SWAP::dogePerpCtMul);
                        } else if(m_instrument == "BTC-USDT-SWAP") {
                            factor = (okx::BTC_USDT_SWAP::btcPerpCtVal) * (okx::BTC_USDT_SWAP::btcPerpCtMul);
                        }
                        if(clOrdId != "") {
                            uint64_t key = std::stoull(clOrdId);
                            auto iterator = this->orderMap.find(key);
                            if(iterator == this->orderMap.end()) {
                                LoggerSingleton::get().infra().warning(
                                    "okx order not placed from this strat run with client order id: ", key);
                            } else {
                                auto order = iterator->second;
                                order->m_reason = RejectReason::NONE;
                                if(orderData["state"] == "live") {
                                    order->m_status = OrderStatus::LIVE;
                                    order->m_orderHasBeenLive = true;
                                    if(orderData["px"] != "")
                                        order->m_priceOnExch = std::stod(orderData["px"].get<std::string>());
                                    if(orderData["sz"] != "")
                                        order->m_qtyOnExch = std::stod(orderData["sz"].get<std::string>()) * factor;
                                    order->m_exchangeOrderId = std::stoull(orderData["ordId"].get<std::string>());
                                } else if(orderData["state"] == "canceled") {
                                    order->m_status = OrderStatus::CANCELED;
                                    cancelQueue.push(order->m_clientOrderId);
                                    maintainOrderLimit();
                                    order->m_cumFilledQty =
                                        std::stod(orderData["accFillSz"].get<std::string>()) * factor;
                                } else if(orderData["state"] == "partially_filled") {
                                    order->m_status = OrderStatus::PARTIALLY_FILLED;
                                    order->m_cumFilledQty =
                                        std::stod(orderData["accFillSz"].get<std::string>()) * factor;
                                    if(orderData.contains("fillPx")) {
                                        double fillPx = std::stod(orderData["fillPx"].get<std::string>());
                                        order->m_fillPx = fillPx;
                                    }
                                    if (orderData.contains("fillTime")) {
                                        order->m_executedTS = std::stoull(orderData["fillTime"].get<std::string>()) * 1000000ULL;
                                    }
                                    order->m_executeTSOnOms = helper::get_current_timestamp_ns();
                                    if(orderData.contains("fillSz")) {
                                        /*
                                        if(order->m_side) {
                                            m_exposureQty +=
                                                std::stod(orderData["fillSz"].get<std::string>());
                                        } else {
                                            m_exposureQty -=
                                                std::stod(orderData["fillSz"].get<std::string>());
                                        }
                                        */
                                        double fillSz = std::stod(orderData["fillSz"].get<std::string>());
                                        m_positionManager.update_position_by_fillsz(fillSz, order->m_side);
                                        order->m_fillSz = fillSz * factor;
                                    }
                                    if(orderData.contains("fillPnl")) {
                                        m_realisedPnl += std::stod(orderData["fillPnl"].get<std::string>());
                                    }
                                    if(orderData.contains("fillFee")) {
                                        double fillFee = std::stod(orderData["fillFee"].get<std::string>());
                                        fillFee = -1 * fillFee;
                                        m_realisedPnl += fillFee;
                                        order->m_cumFee += fillFee;
                                        order->m_fillFee = fillFee;
                                    }
                                    if(orderData.contains("execType")) {
                                        if(orderData["execType"] == "T") {
                                            order->m_fillMaker = false;
                                        } else {
                                            order->m_fillMaker = true;
                                        }
                                    }
                                    if(orderData.contains("tradeId")) {
                                        order->m_transactionId = orderData["tradeId"];
                                    }
                                } else if(orderData["state"] == "filled") {
                                    order->m_status = OrderStatus::FILLED;
                                    filledQueue.push(order->m_clientOrderId);
                                    maintainOrderLimit();
                                    order->m_cumFilledQty =
                                        std::stod(orderData["accFillSz"].get<std::string>()) * factor;
                                    if(orderData.contains("fillSz")) {
                                        /*
                                        if(order->m_side) {
                                            m_exposureQty +=
                                                std::stod(orderData["fillSz"].get<std::string>());
                                        } else {
                                            m_exposureQty -=
                                                std::stod(orderData["fillSz"].get<std::string>());
                                        }
                                        */
                                        double fillSz = std::stod(orderData["fillSz"].get<std::string>());
                                        m_positionManager.update_position_by_fillsz(fillSz, order->m_side);
                                        order->m_fillSz = fillSz * factor;
                                    }
                                    if(orderData.contains("fillPx")) {
                                        double fillPx = std::stod(orderData["fillPx"].get<std::string>());
                                        order->m_fillPx = fillPx;
                                    }
                                    if(orderData.contains("fillPnl")) {
                                        m_realisedPnl += std::stod(orderData["fillPnl"].get<std::string>());
                                    }
                                    if(orderData.contains("fillFee")) {
                                        double fillFee = std::stod(orderData["fillFee"].get<std::string>());
                                        fillFee = -1 * fillFee;
                                        m_realisedPnl += fillFee;
                                        order->m_cumFee += fillFee;
                                        order->m_fillFee = fillFee;
                                    }
                                    if(orderData.contains("fillTime")) {
                                        order->m_executedTS =
                                            std::stoull(orderData["fillTime"].get<std::string>()) * 1000000ULL;
                                    }
                                    order->m_executeTSOnOms = helper::get_current_timestamp_ns();
                                    if(orderData.contains("execType")) {
                                        if(orderData["execType"] == "T") {
                                            order->m_fillMaker = false;
                                        } else {
                                            order->m_fillMaker = true;
                                        }
                                    }
                                    if(orderData.contains("tradeId")) {
                                        order->m_transactionId = orderData["tradeId"];
                                    }
                                }
                                if(orderStatusUpdateCallback) {
                                    orderStatusUpdateCallback(*order);
                                }
                            }
                        } else {
                            LoggerSingleton::get().infra().warning("okx order not placed from this strat run");
                        }
                    }
                }
            }
        }
        return;
    }

public:
    std::unordered_map<uint64_t, std::shared_ptr<OrderHandler>> orderMap;
    double m_realisedPnl = 0.0;
    double m_exposureQty = 0.0;
    std::queue<uint64_t> cancelQueue;
    std::queue<uint64_t> rejectedQueue;
    std::queue<uint64_t> filledQueue;
    // std::unordered_map<uint64_t,OrderHandler*>
private:
    const std::string m_instrument = "";
    std::unique_ptr<OkxOrderRouter> okxOrderRouter;
    OrderStatusUpdateCallback orderStatusUpdateCallback;
    WebSocketStatusUpdateCallback websocketStatusUpdateCallback;
    OkxPositionManager& m_positionManager;
    std::mutex m_mutex;
    mutable std::mutex m_mutableMutex;
    uint64_t microToNano = 1000;
    const uint32_t retry_limit = 0;
    OkxClient m_client;
    uint32_t m_trackOrderCnt;
};
