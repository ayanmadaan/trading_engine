// infra/market_data.hpp
#pragma once
#include <string>

struct MarketData {
    // std::string instrument;
    // double price;
    // double volume;
    // Add other fields as needed, like timestamp, bid/ask, etc.
    std::string message;
};
/*
#include <exception>
#include <string>
#include <unordered_map>
#include <vector>
struct MdInfo {
    public:
    MdInfo();
    MdInfo(std::string exchnange): m_exchangeName(exchnange){}
    std::string getExchangeName() {
        return this->m_exchangeName;
    }

    std::string setExchangeName(std::string exchangeName) {
        this->m_exchangeName = exchangeName;
    }

    int setDataType(std::string data) {
        try {
            this->m_exchData.push_back(data);
            return 1;
        } catch(const std::bad_alloc& e) {
            return -1;
        }
    }

    int setInstrumetData(std::string data) {
        try {
            this->m_instrumentData.push_back(data);
            return 1;
        } catch(const std::bad_alloc& e) {
            return -1;
        }
    }

    bool m_subscribeToFills = false;
    private:
    std::string m_exchangeName;
    std::vector<std::string> m_exchData;
    std::vector<std::string> m_instrumentData;

};
*/
