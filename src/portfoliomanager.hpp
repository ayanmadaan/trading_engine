#include "../oms/bybitordermanager.hpp"
#include "../oms/okxordermanager.hpp"
#include "../oms/orderhandler.hpp"
#include <unordered_map>
#include <vector>
class PortfolioManager {
public:
    void setByBitOrderManager(ByBitOrderManager* manager) { bybitOrderManager = manager; }

    void setOkxOrderManager(OkxOrderManager* manager) { okxOrderManager = manager; }

    std::vector<OrderHandler> getOpenPositions() {
        /*Open position means positions where the trade has opened but not closed yet
            Idea is to retrieve all the open positions across exchanges
            Order manager of respective exchanges must be precise*/
        // for(auto)
        return {};
    }

    double getOpenPositionOkx() {
        double qty = 0.0;
        for(auto it = okxOrderManager->orderMap.begin(); it != okxOrderManager->orderMap.end(); it++) {
            if(it->second.get()->m_status == OrderStatus::PARTIALLY_FILLED ||
               it->second.get()->m_status == OrderStatus::FILLED) {
                if(it->second.get()->m_side) {
                    qty += it->second.get()->m_cumFilledQty;
                } else {
                    qty -= it->second.get()->m_cumFilledQty;
                }
            }
        }
        return qty;
    }

    double getOpenPositionByBit() {
        double qty = 0.0;
        for(auto it = bybitOrderManager->orderMap.begin(); it != bybitOrderManager->orderMap.end(); it++) {
            if(it->second.get()->m_status == OrderStatus::PARTIALLY_FILLED ||
               it->second.get()->m_status == OrderStatus::FILLED) {
                if(it->second.get()->m_side) {
                    qty += it->second.get()->m_cumFilledQty;
                } else {
                    qty -= it->second.get()->m_cumFilledQty;
                }
            }
        }
        return qty;
    }

    double getCrossExchangeExposure() {
        /*
            Exposure here means qty

            ideally should return exposure as {"instrumentName","qty"} for now accomodate only one instrumentName
           becuase instrument name is exchange specific
        */
        double qty = 0.0;
        qty += getOpenPositionOkx();
        qty += getOpenPositionByBit();
        return qty;
    }

    std::vector<uint64_t> getPendingByBitOrders() {
        std::vector<uint64_t> res;
        for(auto it = bybitOrderManager->orderMap.begin(); it != bybitOrderManager->orderMap.end(); it++) {
            if(it->second.get()->m_status == OrderStatus::LIVE) {
                res.push_back(it->first);
            }
        }
        return res;
    }

    std::vector<uint64_t> getPendingOkxOrders() {
        std::vector<uint64_t> res;
        for(auto it = okxOrderManager->orderMap.begin(); it != okxOrderManager->orderMap.end(); it++) {
            if(it->second.get()->m_status == OrderStatus::LIVE) {
                res.push_back(it->first);
            }
        }
        return res;
    }

    void closeOpenPositions() {}

    double getExposureOnByBit() { return bybitOrderManager->m_exposureQty; }

    double getExposureOnOkx() { return okxOrderManager->m_exposureQty; }

private:
    ByBitOrderManager* bybitOrderManager;
    OkxOrderManager* okxOrderManager;
};
