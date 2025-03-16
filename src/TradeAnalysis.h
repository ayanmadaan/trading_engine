#include <chrono>
#include <cmath>
#include <nlohmann/json.hpp>
#include <vector>

class TradeAnalysis {
public:
    struct Trade {
        std::chrono::system_clock::time_point timestamp;
        double price;
        double quantity;
        bool is_buy; // true for buy, false for sell
        bool is_maker; // true for maker, false for taker
    };

    // Basic counts
    [[nodiscard]] size_t total_trade_count() const { return buy_count() + sell_count(); }
    [[nodiscard]] size_t buy_count() const { return buy_trades_.size(); }
    [[nodiscard]] size_t sell_count() const { return sell_trades_.size(); }

    // Maker/Taker metrics
    [[nodiscard]] size_t maker_count() const { return maker_trades_count_; }
    [[nodiscard]] size_t taker_count() const { return total_trade_count() - maker_trades_count_; }
    [[nodiscard]] size_t buy_maker_count() const { return buy_maker_count_; }
    [[nodiscard]] size_t buy_taker_count() const { return buy_count() - buy_maker_count_; }
    [[nodiscard]] size_t sell_maker_count() const { return sell_maker_count_; }
    [[nodiscard]] size_t sell_taker_count() const { return sell_count() - sell_maker_count_; }

    [[nodiscard]] double maker_volume() const { return maker_volume_; }
    [[nodiscard]] double taker_volume() const { return total_trading_volume() - maker_volume_; }
    [[nodiscard]] double maker_ratio() const {
        return total_trade_count() > 0 ? static_cast<double>(maker_count()) / total_trade_count() : 0.0;
    }

    // Position metrics
    [[nodiscard]] double delta_long_position() const { return total_buy_quantity_; }
    [[nodiscard]] double delta_short_position() const { return total_sell_quantity_; }
    [[nodiscard]] double net_delta_position() const { return delta_long_position() - delta_short_position(); }

    // Price metrics
    [[nodiscard]] double average_buy_price() const {
        return buy_trades_.empty() ? 0.0 : total_buy_value_ / total_buy_quantity_;
    }
    [[nodiscard]] double average_sell_price() const {
        return sell_trades_.empty() ? 0.0 : total_sell_value_ / total_sell_quantity_;
    }
    [[nodiscard]] double weighted_average_price() const {
        const double total_quantity = total_buy_quantity_ + total_sell_quantity_;
        return total_quantity > 0 ? (total_buy_value_ + total_sell_value_) / total_quantity : 0.0;
    }

    // Volume metrics
    [[nodiscard]] double long_trading_volume() const { return total_buy_value_; }
    [[nodiscard]] double short_trading_volume() const { return total_sell_value_; }
    [[nodiscard]] double total_trading_volume() const { return long_trading_volume() + short_trading_volume(); }

    // Advanced metrics
    [[nodiscard]] double buy_sell_ratio() const {
        return sell_count() > 0 ? static_cast<double>(buy_count()) / sell_count() : 0.0;
    }
    [[nodiscard]] double volume_vwap() const { return total_trading_volume() > 0 ? weighted_average_price() : 0.0; }

    // Risk metrics
    [[nodiscard]] double largest_single_trade_value() const { return max_single_trade_value_; }
    [[nodiscard]] double average_trade_size() const {
        if(size_stats_.count == 0) return 0.0;
        return size_stats_.sum / size_stats_.count;
    }
    [[nodiscard]] double trade_size_volatility() const {
        if(size_stats_.count <= 1) return 0.0;

        double mean = average_trade_size();
        double variance = (size_stats_.sum_squares - 2 * mean * size_stats_.sum + size_stats_.count * mean * mean) /
                          (size_stats_.count - 1);

        return std::sqrt(std::max(0.0, variance));
    }

    // Add a new trade to the analysis
    void add_trade(const Trade& trade) {
        if(trade.is_buy) {
            buy_trades_.push_back(trade);
            total_buy_quantity_ += trade.quantity;
            total_buy_value_ += trade.price * trade.quantity;
            if(trade.is_maker) {
                buy_maker_count_++;
            }
        } else {
            sell_trades_.push_back(trade);
            total_sell_quantity_ += trade.quantity;
            total_sell_value_ += trade.price * trade.quantity;
            if(trade.is_maker) {
                sell_maker_count_++;
            }
        }

        // Update maker/taker metrics
        if(trade.is_maker) {
            maker_trades_count_++;
            maker_volume_ += trade.price * trade.quantity;
        }

        // Update max trade value
        double trade_value = trade.price * trade.quantity;
        max_single_trade_value_ = std::max(max_single_trade_value_, trade_value);

        update_size_statistics(trade.quantity);
    }

    // Reset analysis
    void reset() {
        buy_trades_.clear();
        sell_trades_.clear();
        total_buy_quantity_ = 0.0;
        total_sell_quantity_ = 0.0;
        total_buy_value_ = 0.0;
        total_sell_value_ = 0.0;
        max_single_trade_value_ = 0.0;
        maker_trades_count_ = 0;
        buy_maker_count_ = 0;
        sell_maker_count_ = 0;
        maker_volume_ = 0.0;
    }

    // Add this function before the private section
    [[nodiscard]] nlohmann::json get_status() const {
        nlohmann::json status;

        // Basic counts
        status["counts"] = {{"total", total_trade_count()},
                            {"buys", buy_count()},
                            {"sells", sell_count()},
                            {"maker", maker_count()},
                            {"taker", taker_count()},
                            {"buy_maker", buy_maker_count()},
                            {"buy_taker", buy_taker_count()},
                            {"sell_maker", sell_maker_count()},
                            {"sell_taker", sell_taker_count()}};

        // Position metrics
        status["position"] = {{"delta_long", delta_long_position()},
                              {"delta_short", delta_short_position()},
                              {"net_delta", net_delta_position()}};

        // Price metrics
        status["prices"] = {{"average_buy", average_buy_price()},
                            {"average_sell", average_sell_price()},
                            {"weighted_average", weighted_average_price()}};

        // Volume metrics
        status["volume"] = {{"maker", maker_volume()},
                            {"taker", taker_volume()},
                            {"long", long_trading_volume()},
                            {"short", short_trading_volume()},
                            {"total", total_trading_volume()}};

        // Risk metrics
        status["risk"] = {{"largest_single_trade_value", largest_single_trade_value()},
                          {"average_trade_size", average_trade_size()},
                          {"trade_size_volatility", trade_size_volatility()}};

        // Ratios
        status["ratios"] = {{"maker", maker_ratio()}, {"buy_sell", buy_sell_ratio()}};

        return status;
    }

private:
    std::vector<Trade> buy_trades_;
    std::vector<Trade> sell_trades_;
    double total_buy_quantity_ = 0.0;
    double total_sell_quantity_ = 0.0;
    double total_buy_value_ = 0.0;
    double total_sell_value_ = 0.0;
    double max_single_trade_value_ = 0.0;

    // Maker/Taker tracking
    size_t maker_trades_count_ = 0;
    size_t buy_maker_count_ = 0;
    size_t sell_maker_count_ = 0;
    double maker_volume_ = 0.0;

    struct SizeStatistics {
        double sum = 0.0;
        double sum_squares = 0.0;
        size_t count = 0;
    } size_stats_;

    void update_size_statistics(double size) {
        size_stats_.sum += size;
        size_stats_.sum_squares += size * size;
        size_stats_.count++;
    }
};
