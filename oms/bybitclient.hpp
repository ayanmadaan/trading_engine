#pragma once

#include "../utils/connections.hpp"
#include "../utils/logger.hpp"
#include "exchangeclient.hpp"
#include <chrono>
#include <curl/curl.h>
#include <iomanip>
#include <nlohmann/json.hpp> // Include nlohmann/json
#include <openssl/hmac.h>
#include <sstream>
#include <stdexcept>

class BybitClient : public ExchangeClient<BybitClient> {
public:
    BybitClient(const bool tradingMode, const std::string& apiKey, const std::string& apiSecret)
        : m_apiKey(apiKey)
        , m_apiSecret(apiSecret)
        , m_recvWindow("5000") {
        m_curl = curl_easy_init();
        if(tradingMode) {
            m_baseUrl = Connections::getByBitLiveCurlBaseUrl();
        } else {
            m_baseUrl = Connections::getByBitTestCurlBaseUrl();
        }
        if(!m_curl) {
            throw std::runtime_error("Failed to initialize CURL");
        }
    }

    ~BybitClient() {
        if(m_curl) {
            curl_easy_cleanup(m_curl);
        }
    }

    [[nodiscard]]
    std::pair<bool, double> fetch_position_impl(const std::string instType, const std::string symbol) const {
        std::string category = instType;
        auto response = getPositions(category, symbol);
        LoggerSingleton::get().plain().curl_response("bybit fetch position response: ", response.body);
        if(!response.success) {
            return {false, 0.0};
        }

        // Parse response using nlohmann::json
        try {
            auto jsonResponse = nlohmann::json::parse(response.body);
            std::string side = jsonResponse["result"]["list"][0]["side"].get<std::string>();
            double size = jsonResponse["result"]["list"][0].value("size", "").empty()
                              ? 0.0
                              : std::stod(jsonResponse["result"]["list"][0]["size"].get<std::string>());
            if(side == "Sell") {
                size = -1 * size;
            }
            return {true, size};
        } catch(const nlohmann::json::exception& e) {
            return {false, 0.0};
        }
    }

    [[nodiscard]]
    bool cancelAllOpenOrders() const {
        std::string category = "linear";
        std::string settleCoin = "USDT";
        auto response = sendCancelAll(category, settleCoin);
        if(!response.success) {
            LoggerSingleton::get().infra().error("failed to cancel all open orders: ", response.body);
            return false;
        }

        try {
            auto jsonResponse = nlohmann::json::parse(response.body);
            LoggerSingleton::get().plain().curl_response("bybit cancel all open orders response: ",
                                                         jsonResponse.dump());
            if(jsonResponse.contains("result") && jsonResponse["result"].contains("success")) {
                std::string success = jsonResponse["result"]["success"].get<std::string>();
                if(success == "1") {
                    LoggerSingleton::get().infra().info("successfully cancelled all bybit open orders.");
                    return true;
                } else {
                    LoggerSingleton::get().infra().error("failed to cancel orders: ", jsonResponse.dump());
                    return false;
                }
            }
        } catch(const nlohmann::json::exception& e) {
            LoggerSingleton::get().infra().error("json parsing error: ", e.what());
            return false;
        }
        return false;
    }

    [[nodiscard]]
    std::pair<bool, std::string>
    getTradeHistory(std::string category, std::string symbol, uint64_t startTime = 0, uint64_t endTime = 0) const {
        std::ostringstream queryParams;
        queryParams << "category=" << category;
        if(!symbol.empty()) {
            queryParams << "&symbol=" << symbol;
        }
        if(startTime > 0) {
            queryParams << "&startTime=" << startTime;
        }
        if(endTime > 0) {
            queryParams << "&endTime=" << endTime;
        }
        Response response = makeRequest("/v5/execution/list", queryParams.str());
        if(!response.success) {
            LoggerSingleton::get().infra().error("failed to fetch trade history: ", response.error);
            return {false, "Failed to fetch trade history"};
        }
        LoggerSingleton::get().plain().curl_response("bybit trade history curl response: ", response.body);
        return {true, response.body};
    }

    [[nodiscard]]
    std::pair<bool, double> getRealisedPnlOfCurrentDay(std::string category = "linear") const {
        std::ostringstream queryParams;
        queryParams << "category=" << category;
        uint64_t startTime = helper::start_of_current_day_utc();
        queryParams << "&startTime=" << startTime;
        Response response = makeRequest("/v5/position/closed-pnl", queryParams.str());
        LoggerSingleton::get().plain().curl_response("bybit realised pnl response: ", response.body);
        if(!response.success) {
            LoggerSingleton::get().infra().error("failed to fetch realised pnl on bybit ", response.error);
            return {false, 0.0};
        } else {
            double pnl = parseRealisedPnl(response.body);
            return {true, pnl};
        }
    }

    [[nodiscard]]
    std::pair<bool, double>
    getRealisedPnlBetweenTimeInterval(uint64_t startTime, uint64_t endTime, std::string category = "linear") const {
        std::ostringstream queryParams;
        queryParams << "category=" << category;
        queryParams << "&startTime=" << startTime;
        queryParams << "&endTime=" << endTime;
        Response response = makeRequest("/v5/position/closed-pnl", queryParams.str());
        LoggerSingleton::get().plain().curl_response("bybit realised pnl response between time interval: ",
                                                     response.body);
        if(!response.success) {
            LoggerSingleton::get().infra().error("failed to fetch realised pnl on bybit between time interval",
                                                 response.error);
            return {false, 0.0};
        } else {
            double pnl = parseRealisedPnl(response.body);
            return {true, pnl};
        }
    }

    [[nodiscard]]
    std::pair<bool, double> getUnrealisedPnl(std::string category = "linear") const {
        std::ostringstream queryParams;
        std::string settle = "USDT";
        queryParams << "category=" << category;
        queryParams << "&settleCoin=" << settle;
        Response response = makeRequest("/v5/position/list", queryParams.str());
        LoggerSingleton::get().plain().curl_response("bybit unrealised pnl response: ", response.body);
        if(!response.success) {
            LoggerSingleton::get().infra().error("failed to fetch unrealised pnl on bybit ", response.error);
            return {false, 0.0};
        } else {
            double pnl = parseUnrealisedPnl(response.body);
            return {true, pnl};
        }
    }

private:
    std::string m_apiKey;
    std::string m_apiSecret;
    std::string m_baseUrl;
    std::string m_recvWindow;
    CURL* m_curl;

    struct Response {
        long httpCode;
        std::string body;
        std::string error;
        bool success;
    };

    Response sendCancelAll(std::string& category, std::string& settleCoin) const {
        std::string queryParams = "category=" + category + "&settleCoin=" + settleCoin;
        return makeRequest("/v5/order/cancel-all", queryParams, "POST");
    }

    Response getPositions(const std::string& category, const std::string& symbol) const {
        std::string queryParams = "category=" + category + "&symbol=" + symbol;
        return makeRequest("/v5/position/list", queryParams);
    }

    std::string getTimestamp() const {
        auto now = std::chrono::system_clock::now();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return std::to_string(millis);
    }

    std::string generateSignature(const std::string& timestamp, const std::string& queryParams) const {
        std::string signaturePayload = timestamp + m_apiKey + m_recvWindow + queryParams;
        unsigned char* digest = HMAC(EVP_sha256(),
                                     m_apiSecret.c_str(),
                                     m_apiSecret.length(),
                                     reinterpret_cast<const unsigned char*>(signaturePayload.c_str()),
                                     signaturePayload.length(),
                                     nullptr,
                                     nullptr);
        std::stringstream ss;
        for(int i = 0; i < 32; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
        }
        return ss.str();
    }

    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        userp->append(static_cast<char*>(contents), size * nmemb);
        return size * nmemb;
    }

    Response
    makeRequest(const std::string& endpoint, const std::string& queryParams, const std::string& method = "GET") const {
        CURL* curl = curl_easy_init();
        Response response{0, "", "", false};
        std::string responseData;

        std::string url = m_baseUrl + endpoint + "?" + queryParams;
        std::string timestamp = getTimestamp();
        std::string signature = generateSignature(timestamp, queryParams);

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("X-BAPI-API-KEY: " + m_apiKey).c_str());
        headers = curl_slist_append(headers, ("X-BAPI-SIGN: " + signature).c_str());
        headers = curl_slist_append(headers, ("X-BAPI-TIMESTAMP: " + timestamp).c_str());
        headers = curl_slist_append(headers, ("X-BAPI-RECV-WINDOW: " + m_recvWindow).c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        if(method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, queryParams.c_str());
        } else {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            url += "?" + queryParams;
        }

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);

        std::string curlRequestStr = formatCurlRequest(endpoint, queryParams, method, headers);
        LoggerSingleton::get().plain().curl_request("bybit curl request: ", curlRequestStr);

        CURLcode res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            response.error = curl_easy_strerror(res);
        } else {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.httpCode);
            response.body = responseData;
            response.success = true;
        }
        curl_easy_cleanup(curl);

        curl_slist_free_all(headers);
        return response;
    }

    std::string formatCurlRequest(const std::string& endpoint,
                                  const std::string& queryParams,
                                  const std::string& method,
                                  struct curl_slist* headers) const {
        std::ostringstream curlRequest;

        std::string url = m_baseUrl + endpoint;
        if(method == "GET" && !queryParams.empty()) {
            url += "?" + queryParams;
        }

        curlRequest << "curl request:";
        curlRequest << "url: " << url;

        return curlRequest.str();
    }

    double parseRealisedPnl(std::string& message) const {
        double pnl = 0.0;
        nlohmann::json parsedMessage = nlohmann::json::parse(message);
        if(parsedMessage.contains("retMsg") && parsedMessage["retMsg"] == "OK") {
            if(parsedMessage.contains("result") && parsedMessage["result"].contains("list") &&
               parsedMessage["result"]["list"].is_array()) {
                for(auto& item : parsedMessage["result"]["list"]) {
                    if(item.contains("closedPnl")) {
                        pnl += std::stod(item["closedPnl"].get<std::string>());
                    }
                }
            }
        }
        return pnl;
    }

    double parseUnrealisedPnl(std::string& message) const {
        double pnl = 0.0;
        nlohmann::json parsedMessage = nlohmann::json::parse(message);
        if(parsedMessage.contains("retMsg") && parsedMessage["retMsg"] == "OK") {
            if(parsedMessage.contains("result") && parsedMessage["result"].contains("list") &&
               parsedMessage["result"]["list"].is_array()) {
                for(auto& item : parsedMessage["result"]["list"]) {
                    if(item.contains("unrealisedPnl")) {
                        pnl += std::stod(item["unrealisedPnl"].get<std::string>());
                    }
                }
            }
        }
        return pnl;
    }
};
