#pragma once

#include "../utils/connections.hpp"
#include "../utils/logger.hpp"
#include "../utils/staticparams.hpp"
#include "exchangeclient.hpp"
#include <chrono>
#include <curl/curl.h>
#include <iomanip>
#include <nlohmann/json.hpp> // Include nlohmann/json
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <sstream>
#include <stdexcept>

class OkxClient : public ExchangeClient<OkxClient> {
public:
    OkxClient(const bool tradingMode,
              const std::string& apiKey,
              const std::string& apiSecret,
              const std::string& passphrase)
        : tradingMode(tradingMode)
        , m_apiKey(apiKey)
        , m_apiSecret(apiSecret)
        , m_passphrase(passphrase) {
        m_curl = curl_easy_init();
        if(tradingMode) {
            m_baseUrl = Connections::getOkxLiveCurlBaseUrl();
        } else {
            m_baseUrl = Connections::getOkxMockCurlBaseUrl();
        }
        if(!m_curl) {
            throw std::runtime_error("Failed to initialize CURL");
        }
    }

    ~OkxClient() {
        if(m_curl) {
            curl_easy_cleanup(m_curl);
        }
    }

    [[nodiscard]]
    std::pair<bool, double> fetch_position_impl(const std::string instType, const std::string symbol) const {
        std::string instId = symbol;
        auto response = getPositions(instType, instId);
        LoggerSingleton::get().plain().curl_response("okx fetch position response: ", response.body);
        if(!response.success) {
            return {false, 0.0};
        }

        // Parse response using nlohmann::json
        try {
            auto jsonResponse = nlohmann::json::parse(response.body);
            if(jsonResponse["data"].empty()) {
                return {true, 0.0}; // Or handle it appropriately
            }
            double positionVal = jsonResponse["data"][0].value("pos", "").empty()
                                     ? 0.0
                                     : std::stod(jsonResponse["data"][0]["pos"].get<std::string>());
            std::string_view instId = jsonResponse["data"][0]["instId"].get<std::string>();
            if(instId == "DOGE-USDT-SWAP") {
                positionVal = positionVal * (okx::DOGE_USDT_SWAP::dogePerpCtVal) * (okx::DOGE_USDT_SWAP::dogePerpCtMul);
            } else if(instId == "BTC-USDT-SWAP") {
                positionVal = positionVal * (okx::BTC_USDT_SWAP::btcPerpCtMul) * (okx::BTC_USDT_SWAP::btcPerpCtVal);
            }
            return {true, positionVal};
        } catch(const nlohmann::json::exception& e) {
            // throw std::runtime_error("Failed to parse position data: " + std::string(e.what()));
            return {false, 0.0};
        }
    }

    std::pair<bool, std::vector<nlohmann::json>> fetchOpenPositions() const {
        std::vector<nlohmann::json> orders;
        try {
            std::string endpoint = "/api/v5/trade/orders-pending";
            std::string queryParams = "";
            auto response = makeRequest("GET", endpoint);

            if(!response.success) {
                LoggerSingleton::get().infra().error("failed to fetch open orders: ", response.error);
                return {false, orders};
            }

            // Parse the response
            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
            LoggerSingleton::get().plain().curl_response("fetch okx open orders curl response: ", jsonResponse.dump());
            if(jsonResponse["code"] != "0") {
                LoggerSingleton::get().infra().error("error fetching open orders: ", jsonResponse.dump());
                return {false, orders};
            }

            // Step 2: Extract order IDs and instruments
            for(const auto& order : jsonResponse["data"]) {
                orders.push_back(
                    {{"instId", order["instId"].get<std::string>()}, {"ordId", order["ordId"].get<std::string>()}});
            }

            if(orders.empty()) {
                LoggerSingleton::get().infra().info("no open orders to cancel.");
                return {true, orders};
            }
            return {true, orders};
        } catch(const std::exception& e) {
            LoggerSingleton::get().infra().error("exception in fetch open positions: ", e.what());
            return {false, orders};
        }
    }

    bool cancelAll() const {
        try {
            // Step 1: Fetch all open orders
            auto res = fetchOpenPositions();
            if(!res.first) {
                LoggerSingleton::get().infra().error("failed to fetch all open orders: ");
                return false;
            }
            std::vector<nlohmann::json> orders = res.second;
            // Step 3: Cancel orders in batches of up to 20
            std::string cancelEndpoint = "/api/v5/trade/cancel-batch-orders";
            const size_t batchSize = 20;
            size_t totalOrders = orders.size();
            for(size_t i = 0; i < totalOrders; i += batchSize) {
                // Prepare a batch
                size_t end = std::min(i + batchSize, totalOrders);
                std::vector<nlohmann::json> batch(orders.begin() + i, orders.begin() + end);
                nlohmann::json requestBody = batch;

                // Send the cancel request
                std::string requestBodyStr = requestBody.dump();
                auto cancelResponse = makeRequest("POST", cancelEndpoint, requestBodyStr);

                if(!cancelResponse.success) {
                    LoggerSingleton::get().infra().error("batch cancel failed: ", cancelResponse.error);
                    return false;
                }

                // Log the response
                nlohmann::json cancelJsonResponse = nlohmann::json::parse(cancelResponse.body);
                LoggerSingleton::get().plain().curl_response("cancel json response: ", cancelJsonResponse);
                if(cancelJsonResponse["code"] != "0") {
                    LoggerSingleton::get().infra().error("error in cancel batch response");
                    return false;
                }
                LoggerSingleton::get().infra().info("successfully cancelled batch: ", requestBodyStr);
            }
            LoggerSingleton::get().infra().info("successfully cancelled all okx open orders.");
            return true;

        } catch(const std::exception& e) {
            LoggerSingleton::get().infra().error("exception in cancelAll: ", e.what());
            return false;
        }
    }

    std::pair<bool, std::string> getTradeHistory(const std::string& instType,
                                                 const std::string& instId = "",
                                                 uint64_t begin = 0,
                                                 uint64_t end = 0,
                                                 int limit = 100) const {
        std::string endpoint = "/api/v5/trade/fills-history";
        std::ostringstream queryParams;

        queryParams << "instType=" << instType; // Required parameter

        if(!instId.empty()) queryParams << "&instId=" << instId;
        if(begin > 0) queryParams << "&begin=" << begin;
        if(end > 0) queryParams << "&end=" << end;
        queryParams << "&limit=" << limit;

        Response response = makeRequest("GET", endpoint + "?" + queryParams.str());
        LoggerSingleton::get().plain().curl_response("okx trade history curl response: ", response.body);
        if(!response.success) {
            LoggerSingleton::get().infra().error("failed to fetch trade history: ", response.error);
            return {false, "failed to fetch trade history"};
        }
        return {true, response.body};
    }

    [[nodiscard]]
    std::pair<bool, double> getRealisedPnlOfCurrentDay() const {
        std::string endpoint = "/api/v5/account/positions-history";
        std::string instType = "SWAP";
        uint64_t dayBegin = helper::start_of_current_day_utc();
        uint64_t currTs = helper::get_current_timestamp_ms();
        std::ostringstream queryParams;
        queryParams << "instType=" << instType;
        queryParams << "&begin=" << dayBegin;
        queryParams << "&end=" << currTs;
        Response response = makeRequest("GET", endpoint + "?" + queryParams.str());
        LoggerSingleton::get().plain().curl_response("okx realised pnl curl response: ", response.body);
        if(!response.success) {
            LoggerSingleton::get().infra().error("failed to fetch realised pnl from okx: ", response.error);
            return {false, 0.0};
        } else {
            double pnl = parseRealisedPnl(response.body);
            return {true, pnl};
        }
    }

    [[nodiscard]]
    std::pair<bool, double> getRealisedPnlBetweenTimeInterval(uint64_t startTime, uint64_t endTime) const {
        std::string endpoint = "/api/v5/account/positions-history";
        std::string instType = "SWAP";
        std::ostringstream queryParams;
        queryParams << "instType=" << instType;
        queryParams << "&begin=" << startTime;
        queryParams << "&end=" << endTime;
        Response response = makeRequest("GET", endpoint + "?" + queryParams.str());
        LoggerSingleton::get().plain().curl_response("okx realised pnl curl response between time interval: ",
                                                     response.body);
        if(!response.success) {
            LoggerSingleton::get().infra().error("failed to fetch realised pnl from okx between time interval: ",
                                                 response.error);
            return {false, 0.0};
        } else {
            double pnl = parseRealisedPnl(response.body);
            return {true, pnl};
        }
    }

    [[nodiscard]]
    std::pair<bool, double> getUnrealisedPnl() const {
        std::ostringstream queryParams;
        Response response = makeUnrealisedPnlRequest();
        LoggerSingleton::get().plain().curl_response("okx unrealised pnl curl response: ", response.body);
        if(!response.success) {
            LoggerSingleton::get().infra().error("failed to fetch unrealised pnl from okx: ", response.error);
            return {false, 0.0};
        } else {
            double pnl = parseUnrealisedPnl(response.body);
            return {true, pnl};
        }
    }

private:
    std::string m_apiKey;
    std::string m_apiSecret;
    std::string m_passphrase;
    std::string m_baseUrl;
    CURL* m_curl;
    bool tradingMode;

    struct Response {
        long httpCode;
        std::string body;
        std::string error;
        bool success;
    };

    Response getPositions(const std::string& instType, const std::string& instId) const {
        std::string endpoint = "/api/v5/account/positions";
        std::string queryParams = "instType=" + instType + "&instId=" + instId;
        return makeRequest("GET", endpoint + "?" + queryParams);
    }

    Response makeUnrealisedPnlRequest() const {
        std::string endpoint = "/api/v5/account/positions";
        return makeRequest("GET", endpoint);
    }

    std::string generateTimestamp() const {
        auto now = std::chrono::system_clock::now();
        std::time_t time_now = std::chrono::system_clock::to_time_t(now);
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        std::ostringstream ss;
        std::tm* utc_tm = std::gmtime(&time_now);
        ss << std::put_time(utc_tm, "%Y-%m-%dT%H:%M:%S");
        ss << "." << std::setfill('0') << std::setw(3) << millis << "Z";
        return ss.str();
    }

    std::string generateSignature(const std::string& timestamp,
                                  const std::string& method,
                                  const std::string& requestPath,
                                  const std::string& body) const {
        std::string prehash = timestamp + method + requestPath + body;
        unsigned char* digest = HMAC(EVP_sha256(),
                                     m_apiSecret.c_str(),
                                     m_apiSecret.length(),
                                     reinterpret_cast<const unsigned char*>(prehash.c_str()),
                                     prehash.length(),
                                     nullptr,
                                     nullptr);
        return base64Encode(std::string(reinterpret_cast<char*>(digest), 32));
    }

    std::string base64Encode(const std::string& input) const {
        BIO* b64 = BIO_new(BIO_f_base64());
        BIO* bio = BIO_new(BIO_s_mem());
        BIO_push(b64, bio);
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
        BIO_write(b64, input.c_str(), input.length());
        BIO_flush(b64);
        char* outBuffer = nullptr;
        long length = BIO_get_mem_data(bio, &outBuffer);
        std::string output(outBuffer, length);
        BIO_free_all(b64);
        return output;
    }

    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        userp->append(static_cast<char*>(contents), size * nmemb);
        return size * nmemb;
    }

    Response makeRequest(const std::string& method, const std::string& requestPath, const std::string& body = "") const {
        CURL* curl = curl_easy_init();
        Response response{0, "", "", false};
        std::string responseData;

        std::string url = m_baseUrl + requestPath;
        std::string timestamp = generateTimestamp();
        std::string signature = generateSignature(timestamp, method, requestPath, body);

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("OK-ACCESS-KEY: " + m_apiKey).c_str());
        headers = curl_slist_append(headers, ("OK-ACCESS-SIGN: " + signature).c_str());
        headers = curl_slist_append(headers, ("OK-ACCESS-TIMESTAMP: " + timestamp).c_str());
        headers = curl_slist_append(headers, ("OK-ACCESS-PASSPHRASE: " + m_passphrase).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");

        if(!tradingMode) {
            headers = curl_slist_append(headers, "x-simulated-trading: 1");
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        if(method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        } else if(method == "GET") {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        }
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);

        std::string curlRequestStr = formatCurlRequest(requestPath, "", method, headers);
        LoggerSingleton::get().plain().curl_request("okx curl request: ", curlRequestStr);

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
        if(parsedMessage.contains("code") && parsedMessage["code"] == "0" && parsedMessage.contains("data") &&
           parsedMessage["data"].is_array()) {
            for(auto& item : parsedMessage["data"]) {
                pnl += std::stod(item["realizedPnl"].get<std::string>());
            }
        }
        return pnl;
    }

    double parseUnrealisedPnl(std::string& message) const {
        double pnl = 0.0;
        nlohmann::json parsedMessage = nlohmann::json::parse(message);
        if(parsedMessage.contains("code") && parsedMessage["code"] == "0" && parsedMessage.contains("data") &&
           parsedMessage["data"].is_array()) {
            for(auto& item : parsedMessage["data"]) {
                pnl += std::stod(item["upl"].get<std::string>());
            }
        }
        return pnl;
    }
};
