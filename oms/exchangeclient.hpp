#pragma once

#include <string>
#include <utility>
#include <curl/curl.h>
#include <nlohmann/json.hpp> // Use nlohmann/json

enum class ReconStatus{
    FailedQuery,
    NoGap,
    TolerableGap,
    IntolerableGap,
    UndeterminedGap
};
template <typename Derived>
class ExchangeClient {
public:
    // Fetch position data from the exchange
    std::pair<double, double> fetch_position(const std::string instType, const std::string symbol) {
        return static_cast<Derived*>(this)->fetch_position_impl(instType, symbol);
    }

protected:
    // Helper function for CURL requests
    std::pair<double, double> perform_curl_request(const std::string& url, const std::string& apiKey) {
        CURL* curl;
        CURLcode res;
        std::string readBuffer;

        curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, ("X-API-KEY: " + apiKey).c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);

            if (res == CURLE_OK) {
                try {
                    // Parse JSON response using nlohmann::json
                    auto jsonResponse = nlohmann::json::parse(readBuffer);
                    double exchangeQuantity = jsonResponse["quantity"].get<double>();
                    double exchangePrice = jsonResponse["averagePrice"].get<double>();
                    return {exchangeQuantity, exchangePrice};
                } catch (const nlohmann::json::exception& e) {
                    // Handle JSON parsing errors
                    throw std::runtime_error("Failed to parse JSON response: " + std::string(e.what()));
                }
            }
        }
        return {0.0, 0.0}; // Return default values if there's an error
    }

private:
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }
};