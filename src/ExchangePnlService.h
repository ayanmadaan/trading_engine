#pragma once
#include "format.h"
#include "logging.h"
#include <concepts>
#include <cstdint>
#include <optional>
#include <thread>
#include <utility>
template<typename T>
concept PnlProvider = requires(const T& pnl_provider, uint64_t start_time_ms, uint64_t end_time_ms) {
    { pnl_provider.getUnrealisedPnl() } -> std::convertible_to<std::pair<bool, double>>;
    {
        pnl_provider.getRealisedPnlBetweenTimeInterval(start_time_ms, end_time_ms)
    } -> std::convertible_to<std::pair<bool, double>>;
    { pnl_provider.getRealisedPnlOfCurrentDay() } -> std::convertible_to<std::pair<bool, double>>;
};

template<PnlProvider QuoteProvider, PnlProvider HedgeProvider>
class ExchangePnlService {
public:
    ExchangePnlService(const QuoteProvider& quote_provider, const HedgeProvider& hedge_provider)
        : quote_provider_{quote_provider}
        , hedge_provider_{hedge_provider}
        , realized_pnl_base_{get_realized_pnl_base()}
        , unrealized_pnl_base_{get_unrealized_pnl_base()} {}

    [[nodiscard]] std::optional<double> get_realized_pnl() const {
        const auto pnl = get_realized_pnl_of_current_day();
        if(pnl.has_value()) {
            return pnl.value() - realized_pnl_base_;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<double> get_unrealized_pnl() const {
        const auto pnl = get_unrealized_pnl_accumulated();
        if(pnl.has_value()) {
            return pnl.value() - unrealized_pnl_base_;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<double> get_total_pnl() const {
        const auto realized_pnl = get_realized_pnl();
        const auto unrealized_pnl = get_unrealized_pnl();
        if(realized_pnl.has_value() && unrealized_pnl.has_value()) {
            return realized_pnl.value() + unrealized_pnl.value();
        }
        return std::nullopt;
    }

private:
    [[nodiscard]] std::pair<bool, double> get_quote_realized_pnl_of_current_day() const {
        return quote_provider_.getRealisedPnlOfCurrentDay();
    }

    [[nodiscard]] std::pair<bool, double> get_hedge_realized_pnl_of_current_day() const {
        return hedge_provider_.getRealisedPnlOfCurrentDay();
    }

    [[nodiscard]] std::pair<bool, double> get_quote_unrealized_pnl() const {
        return quote_provider_.getUnrealisedPnl();
    }

    [[nodiscard]] std::pair<bool, double> get_hedge_unrealized_pnl() const {
        return hedge_provider_.getUnrealisedPnl();
    }

    [[nodiscard]] std::optional<double> get_realized_pnl_of_current_day() const {
        const auto quote_pnl = get_quote_realized_pnl_of_current_day();
        const auto hedge_pnl = get_hedge_realized_pnl_of_current_day();
        if(quote_pnl.first && hedge_pnl.first) {
            const auto pnl = quote_pnl.second + hedge_pnl.second;
            return pnl;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<double> get_unrealized_pnl_accumulated() const {
        const auto quote_pnl = get_quote_unrealized_pnl();
        const auto hedge_pnl = get_hedge_unrealized_pnl();
        if(quote_pnl.first && hedge_pnl.first) {
            const auto pnl = quote_pnl.second + hedge_pnl.second;
            return pnl;
        }
        return std::nullopt;
    }

    [[nodiscard]] double get_realized_pnl_base() const {
        constexpr int max_attempts = 6;
        constexpr std::chrono::seconds retry_delay(10);

        for(int attempt = 1; attempt <= max_attempts; ++attempt) {
            try {
                bool is_final_attempt = (attempt == max_attempts);

                // Get quote PNL
                const auto quote_pnl = get_quote_realized_pnl_of_current_day();
                if(!quote_pnl.first) {
                    if(is_final_attempt) {
                        log_action_fail<LogLevel::ERROR>("get_realized_pnl_base",
                                                         "quote_pnl_fetch_failed",
                                                         f("attempt", attempt),
                                                         f("max_attempts", max_attempts));
                        throw std::runtime_error("Failed to get realized PnL base: quote PNL fetch failed");
                    } else {
                        log_action_fail<LogLevel::WARNING>("get_realized_pnl_base",
                                                           "quote_pnl_fetch_failed",
                                                           f("attempt", attempt),
                                                           f("max_attempts", max_attempts));
                        std::this_thread::sleep_for(retry_delay);
                        continue;
                    }
                }

                // Get hedge PNL
                const auto hedge_pnl = get_hedge_realized_pnl_of_current_day();
                if(!hedge_pnl.first) {
                    if(is_final_attempt) {
                        log_action_fail<LogLevel::ERROR>("get_realized_pnl_base",
                                                         "hedge_pnl_fetch_failed",
                                                         f("attempt", attempt),
                                                         f("max_attempts", max_attempts));
                        throw std::runtime_error("Failed to get realized PnL base: hedge PNL fetch failed");
                    } else {
                        log_action_fail<LogLevel::WARNING>("get_realized_pnl_base",
                                                           "hedge_pnl_fetch_failed",
                                                           f("attempt", attempt),
                                                           f("max_attempts", max_attempts));
                        std::this_thread::sleep_for(retry_delay);
                        continue;
                    }
                }

                // Success case
                const auto pnl = quote_pnl.second + hedge_pnl.second;
                log_action_pass("get_realized_pnl_base",
                                f("realized_quote_pnl", quote_pnl.second),
                                f("realized_hedge_pnl", hedge_pnl.second),
                                f("realized_pnl", pnl));
                return pnl;

            } catch(const std::exception& e) {
                if(attempt == max_attempts) {
                    log_action_fail<LogLevel::ERROR>("get_realized_pnl_base",
                                                     "exception",
                                                     f("attempt", attempt),
                                                     f("max_attempts", max_attempts),
                                                     f("exception", e.what()));
                    throw std::runtime_error("Failed to get realized PnL base after maximum retry attempts");
                } else {
                    log_action_fail<LogLevel::WARNING>("get_realized_pnl_base",
                                                       "exception",
                                                       f("attempt", attempt),
                                                       f("max_attempts", max_attempts),
                                                       f("exception", e.what()));
                    std::this_thread::sleep_for(retry_delay);
                }
            }
        }

        // This line should never be reached, but is required to avoid compiler warnings
        throw std::runtime_error("Unexpected error in get_realized_pnl_base");
    }


    [[nodiscard]] double get_unrealized_pnl_base() const {
        constexpr int max_attempts = 6;
        constexpr std::chrono::seconds retry_delay(10);

        for(int attempt = 1; attempt <= max_attempts; ++attempt) {
            try {
                bool is_final_attempt = (attempt == max_attempts);

                // Get unrealized PNL
                const auto pnl = get_unrealized_pnl_accumulated();
                if(!pnl.has_value()) {
                    // Use separate log calls based on attempt number
                    if(is_final_attempt) {
                        log_action_fail<LogLevel::ERROR>("get_unrealized_pnl_base",
                                                         "pnl_fetch_failed",
                                                         f("attempt", attempt),
                                                         f("max_attempts", max_attempts));
                        throw std::runtime_error("Failed to get unrealized PNL base after maximum retry attempts");
                    } else {
                        log_action_fail<LogLevel::WARNING>("get_unrealized_pnl_base",
                                                           "pnl_fetch_failed",
                                                           f("attempt", attempt),
                                                           f("max_attempts", max_attempts));
                        std::this_thread::sleep_for(retry_delay);
                        continue;
                    }
                }

                // Success case
                log_action_pass("get_unrealized_pnl_base", f("unrealized_pnl", pnl.value()));
                return pnl.value();

            } catch(const std::exception& e) {
                // Use separate log calls based on attempt number
                if(attempt == max_attempts) {
                    log_action_fail<LogLevel::ERROR>("get_unrealized_pnl_base",
                                                     "exception",
                                                     f("attempt", attempt),
                                                     f("max_attempts", max_attempts),
                                                     f("exception", e.what()));
                    throw std::runtime_error("Failed to get unrealized PNL base after maximum retry attempts");
                } else {
                    log_action_fail<LogLevel::WARNING>("get_unrealized_pnl_base",
                                                       "exception",
                                                       f("attempt", attempt),
                                                       f("max_attempts", max_attempts),
                                                       f("exception", e.what()));
                    std::this_thread::sleep_for(retry_delay);
                }
            }
        }

        // This line should never be reached, but is required to avoid compiler warnings
        throw std::runtime_error("Unexpected error in get_unrealized_pnl_base");
    }

    const QuoteProvider& quote_provider_;
    const HedgeProvider& hedge_provider_;
    const double realized_pnl_base_;
    const double unrealized_pnl_base_;
};
