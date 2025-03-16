#pragma once

#include "../utils/logger.hpp"
#include "CooldownTimer.h"
#include <stdexcept>

class TokenBucketRateLimiter {
public:
    using Clock = CooldownTimer::Clock;
    using Duration = CooldownTimer::Duration;
    using TimePoint = CooldownTimer::TimePoint;

    explicit TokenBucketRateLimiter(int max_actions, Duration time_window, Duration cooldown_duration)
        : max_tokens_(validate_positive(max_actions)), time_window_(validate_positive_duration(time_window)),
          cooldown_timer_(cooldown_duration), tokens_(max_actions), last_refill_time_(Clock::now()) {}

    bool try_consume() { return try_consume(Clock::now()); }

    bool try_consume(TimePoint now) {
        if(cooldown_timer_.is_in_cooldown(now)) {
            return false;
        }

        refill_tokens(now);

        if(tokens_ >= 1) {
            tokens_--;
            return true;
        }

        on_rate_limit_hit(now);
        return false;
    }

    void on_rate_limit_hit() { on_rate_limit_hit(Clock::now()); }

    void on_rate_limit_hit(TimePoint now) {
        cooldown_timer_.start_cooldown(now);
        tokens_ = 0;
    }

    int get_remaining_tokens() { return get_remaining_tokens(Clock::now()); }

    int get_remaining_tokens(TimePoint now) {
        refill_tokens(now);
        return tokens_;
    }

    Duration get_remaining_cooldown_time() const { return get_remaining_cooldown_time(Clock::now()); }

    Duration get_remaining_cooldown_time(TimePoint now) const {
        return cooldown_timer_.get_remaining_cooldown_time(now);
    }

    bool is_in_cooldown() const { return cooldown_timer_.is_in_cooldown(Clock::now()); }

    bool is_in_cooldown(TimePoint now) const { return cooldown_timer_.is_in_cooldown(now); }

private:
    static int validate_positive(int value) {
        if(value <= 0) {
            throw std::invalid_argument("Value must be positive");
        }
        return value;
    }

    static Duration validate_positive_duration(Duration d) {
        if(d <= Duration::zero()) {
            throw std::invalid_argument("Duration must be positive");
        }
        return d;
    }

    void refill_tokens() { refill_tokens(Clock::now()); }

    void refill_tokens(TimePoint now) {
        if(cooldown_timer_.is_in_cooldown(now)) {
            return;
        }

        const auto elapsed = now - last_refill_time_;
        const auto tokens_to_add = (elapsed.count() * static_cast<double>(max_tokens_)) / time_window_.count();
        if(tokens_to_add >= 1.0) {
            tokens_ = std::min(max_tokens_, tokens_ + static_cast<int>(tokens_to_add));
            last_refill_time_ = now;
        }
    }

    const int max_tokens_;
    const Duration time_window_;
    CooldownTimer cooldown_timer_;
    int tokens_;
    TimePoint last_refill_time_;
};
