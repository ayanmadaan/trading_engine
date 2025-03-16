#pragma once

#include "../utils/helper.hpp"
#include "../utils/instrumentmappings.hpp"
#include "../utils/logger.hpp"
#include "bybitclient.hpp"
#include "bybitordersrouting.hpp"
#include "bybitpositionmanager.hpp"
#include "orderhandler.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

class ByBitOrderManager {
public:
    using OrderStatusUpdateCallback = std::function<void(OrderHandler&)>;
    using WebSocketStatusUpdateCallback = std::function<void(bool)>;

    explicit ByBitOrderManager(const bool trading_mode,
                               const std::string& proxy_uri,
                               const uint32_t retry_limit,
                               const uint32_t track_order_cnt,
                               const std::string api_key,
                               const std::string api_secret,
                               ByBitPositionManager& manager)
        : m_positionManager(manager)
        , m_trackOrderCnt(track_order_cnt)
        , retry_limit(retry_limit)
        , m_client(trading_mode, api_key, api_secret) {
        LOG_INFRA_DEBUG("Order track cnt: ", m_trackOrderCnt);
        bybitOrderRouter = std::make_unique<ByBitOrderRouter>(
            trading_mode, proxy_uri, retry_limit, api_key, api_secret, [this](std::string message) {
                this->updateOrderStatus(message);
            });
    }

    void run() { bybitOrderRouter->setupRoutingConnection(); }

    void stop() { bybitOrderRouter->stop(); }

    bool cancelAll() { return m_client.cancelAllOpenOrders(); }

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
        auto order = getUpdatedOrderStatus(message);
        if(order == nullptr) return;
        if(orderStatusUpdateCallback) {
            orderStatusUpdateCallback(*order);
        }
    }

    // Retrieves the current status of an order
    [[nodiscard]]
    OrderStatus getOrderStatus(const uint64_t orderId) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
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
        m_reqId += 1;
        uint64_t clientOrderId =
            bybitOrderRouter->sendOrder(price, qty, buy, m_reqId, inst.instrument, ordType, tdMode, banAmend);
        reqId_to_orderHandler.emplace(m_reqId, orderHandler);
        if(clientOrderId != 0) {
            orderHandler->m_clientOrderId = clientOrderId;
            orderHandler->m_status = OrderStatus::PENDING;
            orderHandler->m_reason = RejectReason::NONE;
            orderHandler->m_side = buy;
            orderHandler->m_qtySubmitted = qty;
            orderHandler->m_priceSubmitted = price;
            orderMap.emplace(clientOrderId, std::move(orderHandler));
        } else {
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
        orderHandler->m_clientOrderId = clientOrderId;
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
        m_reqId += 1;
        uint64_t ret = bybitOrderRouter->sendCancelOrder(clientOrderId, m_reqId, inst.instrument);
        reqId_to_orderHandler.emplace(m_reqId, orderHandler);
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
        bool status = bybitOrderRouter->send_heartbeat();
        if((!status) && websocketStatusUpdateCallback) {
            websocketStatusUpdateCallback(false);
        }
    }

    // Modify an existing order
    uint64_t modifyOrder(uint64_t clientOrderId, double newPrice, double newQty, const std::string& m_instrument) {
        // std::lock_guard<std::mutex> lock(m_mutex);
        auto orderHandlerIterator = orderMap.find(clientOrderId);
        if(orderHandlerIterator == orderMap.end()) {
            std::shared_ptr<OrderHandler> newOrderHandler = createOrderHandler(m_instrument);
            auto [iterator, inserted] = orderMap.emplace(clientOrderId, std::move(newOrderHandler));
            orderHandlerIterator = iterator;
        }
        auto& orderHandler = orderHandlerIterator->second;
        orderHandler->m_clientOrderId = clientOrderId;
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
        m_reqId += 1;
        uint64_t ret = bybitOrderRouter->modifyOrder(clientOrderId, newQty, newPrice, m_reqId, inst.instrument);
        reqId_to_orderHandler.emplace(m_reqId, orderHandler);
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

    bool isWebSocketReady() const { return bybitOrderRouter->isWebsocketReady(); }

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

    std::shared_ptr<OrderHandler> getUpdatedOrderStatus(const std::string& message) {
        json parsedJson = json::parse(message);
        if(parsedJson.contains("reqId") && parsedJson.contains("retCode")) {
            int retCode = parsedJson["retCode"].get<int>();
            uint64_t order_id = 0;
            std::string reqIdStr = parsedJson["reqId"].get<std::string>();
            uint64_t req = std::stoull(reqIdStr);
            auto iterator = reqId_to_orderHandler.find(req);
            if(retCode == 0) {
                if(iterator != reqId_to_orderHandler.end()) {
                    reqId_to_orderHandler.erase(iterator);
                }
                return nullptr;
            }
            if(iterator == reqId_to_orderHandler.end()) {
                LoggerSingleton::get().infra().warning("req id to cl order id not found: ", req);
                for(const auto& [key, value] : reqId_to_orderHandler) {
                    LOG_INFRA_DEBUG("Key: ", key, " Value: ", value);
                }
                return nullptr;
            } else {
                auto order = iterator->second;
                order->m_status = OrderStatus::REJECTED;
                reqId_to_orderHandler.erase(iterator);
                if(parsedJson.contains("header") && parsedJson["header"].contains("Timenow")) {
                    order->m_rejectionTS = std::stoull(parsedJson["header"]["Timenow"].get<std::string>()) * 1000000ULL;
                }
                if(retCode == 10001) {
                    std::string retMsg = parsedJson["retMsg"].get<std::string>();
                    if(retMsg == "Qty invalid") {
                        order->m_reason = RejectReason::ORDER_SIZE_NOT_MULTIPLE_OF_LOT_SIZE;
                        if(!order->m_orderHasBeenLive) {
                            rejectedQueue.push(order->m_clientOrderId);
                            maintainOrderLimit();
                        }
                    } else if(retMsg == "order not modified") {
                        order->m_reason = RejectReason::ORDER_NOT_MODIFIED_NO_CHANGE_IN_PRICE_QTY;
                        if(!order->m_orderHasBeenLive) {
                            rejectedQueue.push(order->m_clientOrderId);
                            maintainOrderLimit();
                        }
                    } else if(retMsg == "Illegal category") {
                        order->m_reason = RejectReason::INSTRUMENT_BLOCKED;
                        if(!order->m_orderHasBeenLive) {
                            rejectedQueue.push(order->m_clientOrderId);
                            maintainOrderLimit();
                        }
                    }
                } else if(retCode == 110001 || retCode == 110019) {
                    order->m_reason = RejectReason::ORDER_DOES_NOT_EXIST_ON_EXCH_ORDERBOOK;
                    rejectedQueue.push(order->m_clientOrderId);
                    maintainOrderLimit();
                } else if(retCode == 110008 || retCode == 110010) {
                    order->m_reason = RejectReason::ORDER_HAS_BEEN_FILLED_OR_CANCELLED;
                    rejectedQueue.push(order->m_clientOrderId);
                    maintainOrderLimit();
                } else if(retCode == 110003 || retCode == 110094) {
                    order->m_reason = RejectReason::ORDER_PRICE_NOT_IN_RANGE;
                    if(!order->m_orderHasBeenLive) {
                        rejectedQueue.push(order->m_clientOrderId);
                        maintainOrderLimit();
                    }
                } else if(retCode == 110004 || retCode == 110012 || retCode == 110052 || retCode == 110007) {
                    order->m_reason = RejectReason::INSUFFICIENT_FUNDS;
                    if(!order->m_orderHasBeenLive) {
                        rejectedQueue.push(order->m_clientOrderId);
                        maintainOrderLimit();
                    }
                } else if(retCode == 110020) {
                    order->m_reason = RejectReason::EXCEEDED_NUMBER_OF_LIVE_ORDERS;
                    if(!order->m_orderHasBeenLive) {
                        rejectedQueue.push(order->m_clientOrderId);
                        maintainOrderLimit();
                    }
                } else if(retCode == 110079) {
                    order->m_reason = RejectReason::ORDER_BEING_PROCESSED_CANNOT_OPERATE_ON_IT;
                    if(!order->m_orderHasBeenLive) {
                        rejectedQueue.push(order->m_clientOrderId);
                        maintainOrderLimit();
                    }
                } else if(retCode == 10006) {
                    order->m_reason = RejectReason::THROTTLE_HIT;
                    if(!order->m_orderHasBeenLive) {
                        rejectedQueue.push(order->m_clientOrderId);
                        maintainOrderLimit();
                    }
                } else if(retCode == 10016 || retCode == 10019 || retCode == 10429) {
                    order->m_reason = RejectReason::EXCHANGE_BUSY;
                    if(!order->m_orderHasBeenLive) {
                        rejectedQueue.push(order->m_clientOrderId);
                        maintainOrderLimit();
                    }
                } else if(retCode == 33004) {
                    order->m_reason = RejectReason::API_KEY_EXPIRED;
                    if(!order->m_orderHasBeenLive) {
                        rejectedQueue.push(order->m_clientOrderId);
                        maintainOrderLimit();
                    }
                } else if(retCode == 10003) {
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
                reqId_to_orderHandler.erase(req);
                return order;
            }
        }
        return nullptr;
    }

    void maintainOrderLimit() {
        std::lock_guard<std::mutex> lock(m_mutex);
        LOG_INFRA_DEBUG("Order track cnt: ", m_trackOrderCnt);
        while(rejectedQueue.size() > m_trackOrderCnt) {
            uint64_t clOrderId = rejectedQueue.front();
            auto it = orderMap.find(clOrderId);
            if(it != orderMap.end()) {
                orderMap.erase(clOrderId);
            }
            rejectedQueue.pop();
        }
    }

public:
    ByBitPositionManager& m_positionManager;
    std::unordered_map<uint64_t, std::shared_ptr<OrderHandler>> orderMap;
    std::unordered_map<uint64_t, std::shared_ptr<OrderHandler>> reqId_to_orderHandler;
    std::queue<uint64_t> cancelQueue;
    std::queue<uint64_t> filledQueue;
    std::queue<uint64_t> rejectedQueue;
    double m_realisedPnl = 0.0;
    double m_exposureQty = 0.0;

private:
    uint64_t m_reqId = 0;
    std::mutex m_mutex;
    mutable std::mutex m_mutableMutex;
    std::unique_ptr<ByBitOrderRouter> bybitOrderRouter;
    std::unordered_map<uint64_t, std::string> clOrderIdToOrderId;
    OrderStatusUpdateCallback orderStatusUpdateCallback;
    WebSocketStatusUpdateCallback websocketStatusUpdateCallback;
    uint64_t microToNano = 1000;
    uint32_t retry_limit = 0;
    uint32_t m_trackOrderCnt = 0;
    BybitClient m_client;
};
