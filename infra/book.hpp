#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <immintrin.h>
#include <unordered_map>
#include <x86intrin.h>

constexpr size_t MAX_LEVELS = 1000;
constexpr double PRICE_EPSILON = 1e-9;
constexpr size_t POOL_SIZE = 1024 * 1024;

struct alignas(64) PriceLevel {
    double price;
    double quantity;

    PriceLevel()
        : price(0.0)
        , quantity(0.0) {}
};

struct alignas(64) PriceLevelArray {
    std::array<PriceLevel, MAX_LEVELS> levels;
    size_t size;
    bool isDescending = false;

    PriceLevelArray(bool descending = false)
        : size(0)
        , isDescending(descending) {}

    inline int findIndex(double price) const {
        int left = 0;
        int right = static_cast<int>(size) - 1;

        while(left <= right) {
            int mid = left + ((right - left) >> 1);
            double midPrice = levels[mid].price;

            if(std::abs(midPrice - price) < PRICE_EPSILON) return mid;
            if(isDescending) {
                if(midPrice > price)
                    left = mid + 1;
                else
                    right = mid - 1;
            } else {
                if(midPrice < price)
                    left = mid + 1;
                else
                    right = mid - 1;
            }
        }
        return left;
    }

    inline void insert(double price, double quantity) {
        int index = findIndex(price);

        if(index < size && std::abs(levels[index].price - price) < PRICE_EPSILON) {
            if(quantity <= PRICE_EPSILON) {
                std::memmove(&levels[index], &levels[index + 1], (size - index - 1) * sizeof(PriceLevel));
                size--;
            } else {
                levels[index].quantity = quantity;
            }
            return;
        }

        if(quantity > PRICE_EPSILON) {
            if(size < MAX_LEVELS) {

                std::memmove(&levels[index + 1], &levels[index], (size - index) * sizeof(PriceLevel));
                levels[index].price = price;
                levels[index].quantity = quantity;

                size++;
            }
        }
    }

    inline void eraseAt(size_t index) {
        if(index >= size) return;

        if(index < size - 1) {
            std::memmove(&levels[index], &levels[index + 1], (size - index - 1) * sizeof(PriceLevel));
        }
        size--;
    }

    inline bool erase(double price) {
        int index = findIndex(price);
        if(index < size && std::abs(levels[index].price - price) < PRICE_EPSILON) {
            eraseAt(index);
            return true;
        }
        return false;
    }

    inline double getBestPrice() const { return size > 0 ? levels[0].price : 0.0; }

    inline double getTotalVolume(size_t numLevels) const {
        double total = 0.0;
        for(size_t i = 0; i < std::min(numLevels, size); ++i) {
            total += levels[i].quantity;
        }
        return total;
    }
};

class Book {
public:
    explicit Book(std::string instrumentName)
        : m_instrumentName(std::move(instrumentName)) {}

    Book(const Book&) = default;
    Book(Book&&) noexcept = default;
    Book& operator=(const Book&) = delete;
    Book& operator=(Book&&) noexcept = default;

    void setBestBid(double price) { m_bestBid = price; }
    void setBestAsk(double price) { m_bestAsk = price; }

    double getBestBid() const { return m_bestBid; }
    double getBestAsk() const { return m_bestAsk; }
    double getMid() const { return (m_bestBid + m_bestAsk) / 2.0; }
    double getSpread() const { return (m_bestAsk - m_bestBid) / ((m_bestAsk + m_bestBid) / 2); }

    const std::string& getInstrumentName() const { return m_instrumentName; }

    std::string getExchangeName() const { return getToken(0); }
    std::string getMarketType() const { return getToken(1); }
    std::string getBaseAsset() const { return getToken(2); }
    std::string getQuoteAsset() const { return getToken(3); }

    PriceLevelArray bidSide{true};
    PriceLevelArray askSide;
    uint64_t m_timestamp = 0;

private:
    const std::string m_instrumentName;
    double m_bestBid = 0.0;
    double m_bestAsk = 0.0;

    std::string getToken(size_t index) const {
        std::vector<std::string> tokens;
        std::stringstream ss(m_instrumentName);
        std::string token;
        while(std::getline(ss, token, '_')) {
            tokens.push_back(token);
        }
        return (index < tokens.size()) ? tokens[index] : "";
    }
};
