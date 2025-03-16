#pragma once

#include <chrono>
#include <optional>

class CooldownTimer {
public:
    using Clock = std::chrono::system_clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;

    explicit CooldownTimer(Duration cooldown_duration)
        : cooldown_duration_(cooldown_duration) {}

    [[nodiscard]] bool is_in_cooldown() const { return is_in_cooldown(Clock::now()); }

    [[nodiscard]] bool is_in_cooldown(TimePoint now) const {
        return cooldown_end_time_.has_value() && now < *cooldown_end_time_;
    }

    void start_cooldown() { start_cooldown(Clock::now()); }

    void start_cooldown(TimePoint now) {
        if(!is_in_cooldown(now)) {
            cooldown_end_time_ = now + cooldown_duration_;
        }
    }

    void restart_cooldown() { restart_cooldown(Clock::now()); }

    void restart_cooldown(TimePoint now) { cooldown_end_time_ = now + cooldown_duration_; }

    [[nodiscard]] Duration get_remaining_cooldown_time() const { return get_remaining_cooldown_time(Clock::now()); }

    [[nodiscard]] Duration get_remaining_cooldown_time(TimePoint now) const {
        if(cooldown_end_time_) {
            if(*cooldown_end_time_ > now) {
                return *cooldown_end_time_ - now;
            }
        }
        return Duration::zero();
    }

    [[nodiscard]] std::optional<TimePoint> get_cooldown_end_time() const { return cooldown_end_time_; }

private:
    Duration cooldown_duration_;
    std::optional<TimePoint> cooldown_end_time_;
};
