#pragma once
#include <string>

enum class OrderStatus { INITIAL, PENDING, LIVE, PARTIALLY_FILLED, FILLED, CANCELED, REJECTED };

enum class RejectReason {
    // Success/default state
    NONE,

    // System/connection issues
    THROTTLE_HIT, // Rate limiting
    WS_FAILURE, // Connection issues

    // Input validation errors
    INVALID_INSTRUMENT, // Invalid symbol/instrument
    ORDER_SIZE_NOT_MULTIPLE_OF_LOT_SIZE, // Size constraints
    ORDER_PRICE_NOT_IN_RANGE, // Price constraints
    POST_ONLY_WILL_TAKE_LIQUIDITY, // Order type constraint

    // Account/limit errors
    INSUFFICIENT_FUNDS, // Not enough balance
    EXCEEDED_NUMBER_OF_LIVE_ORDERS, // Too many open orders

    // Order state errors
    ORDER_DOES_NOT_EXIST_ON_EXCH_ORDERBOOK, // Order not found
    ORDER_HAS_BEEN_FILLED_OR_CANCELLED, // Order already done
    ORDER_BEING_PROCESSED_CANNOT_OPERATE_ON_IT, // Order in transition
    ORDER_NOT_MODIFIED_NO_CHANGE_IN_PRICE_QTY, // Modification request with no change in price or quantity

    // Service availability issues
    SERVICE_TEMPORARILY_UNAVAILABLE,
    API_OFFLINE_OR_UNAVAILABLE,
    EXCHANGE_BUSY,

    // Auth related issues
    API_KEY_EXPIRED,
    API_KEY_DOES_NOT_MATCH_ENV,
    ACCOUNT_BLOCKED,

    // Feature and restriction issues
    FEATURE_UNAVAILABLE_IN_DEMO,
    INSTRUMENT_BLOCKED,
    CANNOT_TRADE_ON_CHOSEN_CRYPTO_DUE_TO_LOCAL_NEWS_AND_REGULATIONS,

    // Unexpected errors
    UNKNOWN_ERROR
};

struct OrderHandler {
public:
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
    uint64_t m_executeTSOnOms = 0;

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
    RejectReason m_reason;
    // std::unique_ptr<OkxOrderRouter> okxOrderRouter;
    OrderHandler(std::string instrument)
        : // okxOrderRouter(std::make_unique<OkxOrderRouter>(uri)),
        m_instrumentId(instrument) {
        m_status = OrderStatus::INITIAL;
        m_reason = RejectReason::NONE;
    }

    OrderStatus getCurrentStatus() { return m_status; }

    std::string getCurrentStatusStr() const {
        std::string status = "";
        OrderStatus curr_state = this->m_status;
        if(curr_state == OrderStatus::PENDING) {
            status = "PENDING";
        } else if(curr_state == OrderStatus::LIVE) {
            status = "LIVE";
        } else if(curr_state == OrderStatus::FILLED) {
            status = "FILLED";
        } else if(curr_state == OrderStatus::PARTIALLY_FILLED) {
            status = "PARTIALLY_FILLED";
        } else if(curr_state == OrderStatus::CANCELED) {
            status = "CANCELLED";
        } else if(curr_state == OrderStatus::REJECTED) {
            status = "REJECTED";
        } else {
            status = "UNKNOWN";
        }
        return status;
    }

    std::string getRejectReasonStr() const {
        RejectReason reason = this->m_reason;
        if(reason == RejectReason::NONE) {
            return "NONE";
        } else if(reason == RejectReason::THROTTLE_HIT) {
            return "THROTTLE_HIT";
        } else if(reason == RejectReason::WS_FAILURE) {
            return "WS_FAILURE";
        } else if(reason == RejectReason::INVALID_INSTRUMENT) {
            return "INVALID_INSTRUMENT";
        } else if(reason == RejectReason::INSUFFICIENT_FUNDS) {
            return "INSUFFICIENT_FUNDS";
        } else if(reason == RejectReason::ORDER_DOES_NOT_EXIST_ON_EXCH_ORDERBOOK) {
            return "ORDER_DOES_NOT_EXIST_ON_EXCH_ORDERBOOK";
        } else if(reason == RejectReason::ORDER_PRICE_NOT_IN_RANGE) {
            return "ORDER_PRICE_NOT_IN_RANGE";
        } else if(reason == RejectReason::POST_ONLY_WILL_TAKE_LIQUIDITY) {
            return "POST_ONLY_WILL_TAKE_LIQUIDITY";
        } else if(reason == RejectReason::UNKNOWN_ERROR) {
            return "UNKNOWN_ERROR";
        } else {
            return "";
        }
    }

    void resest() {
        this->m_clientOrderId = 0;
        this->m_cumFilledQty = 0;
        this->m_priceOnExch = 0;
        this->m_qtyOnExch = 0;
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6); // format for double values
        std::string state = getCurrentStatusStr();
        std::string reject = getRejectReasonStr();
        std::string side = "";
        if(m_side) {
            side = "BUY";
        } else {
            side = "SELL";
        }
        // std::cout<<"State: "<<state<<"\n";
        oss << "OrderHandler {";
        oss << "  Instrument ID: " << m_instrumentId;
        oss << "  Exchange Order ID: " << m_exchangeOrderId;
        oss << "  Client Order ID: " << m_clientOrderId;
        oss << "  Transaction ID: " << m_transactionId;
        oss << "  Total Filled Quantity: " << m_cumFilledQty;
        oss << "  Current Filled Quantity: " << m_fillSz;
        oss << "  Price on Exchange: " << m_priceOnExch;
        oss << "  Quantity on Exchange: " << m_qtyOnExch;
        oss << "  Side: " << side;

        oss << "  Status: " << state;
        oss << "  Reject Reason: " << reject;

        // Timestamp information
        oss << "  Timestamps:";
        oss << "    New Order on OMS TS: " << m_newOrderOnOmsTS;
        oss << "    New Order on Exchange TS: " << m_newOrderOnExchTS;
        oss << "    New Order Confirmation TS: " << m_newOrderConfirmationTS;
        oss << "    Modify Order on OMS TS: " << m_modifyOrderOnOmsTS;
        oss << "    Modify Order on Exchange TS: " << m_modifyOrderOnExchTS;
        oss << "    Modify Order Confirmation TS: " << m_modifyOrderConfirmationTS;
        oss << "    Cancel Order on OMS TS: " << m_cancelOrderOnOmsTS;
        oss << "    Cancel Order on Exchange TS: " << m_cancelOrderOnExchTS;
        oss << "    Cancel Order Confirmation TS: " << m_cancelOrderConfirmationTS;
        oss << "    Rejection TS: " << m_rejectionTS;

        oss << "}";
        return oss.str();
    }
};
