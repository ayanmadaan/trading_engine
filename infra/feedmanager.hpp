#pragma once
#include "../src/strategy.hpp"
#include "book.hpp"
class FeedManager {
public:
    FeedManager();
    void onBookUpdate(Book& book) { strat.onBinanceMarketDataUpdate(book); }
    Strategy strat;
};
