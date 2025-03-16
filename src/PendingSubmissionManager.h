#pragma once

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

template<typename Clock = std::chrono::system_clock>
class PendingSubmissionManager {
public:
    using TimePoint = typename Clock::time_point;
    using Duration = typename Clock::duration;
    using OrderId = uint64_t;

    explicit PendingSubmissionManager(Duration pending_threshold)
        : pending_threshold_(pending_threshold) {}

    bool add(OrderId order_id) { return pending_submissions_.try_emplace(order_id, Clock::now()).second; }

    bool remove(OrderId order_id) { return pending_submissions_.erase(order_id) > 0; }

    // [[nodiscard]] bool has(OrderId order_id) const { return pending_submissions_.contains(order_id); }
    [[nodiscard]] bool has(OrderId order_id) const { return pending_submissions_.count(order_id) > 0; }

    std::vector<OrderId> get_outdated_pending_submissions() const {
        auto current_time = Clock::now();
        std::vector<OrderId> outdated;

        for(const auto& [order_id, submit_time] : pending_submissions_) {
            if(current_time - submit_time >= pending_threshold_) {
                outdated.push_back(order_id);
            }
        }
        return outdated;
    }

    size_t get_outdated_pending_count() const {
        auto current_time = Clock::now();
        size_t count = 0;

        for(const auto& [order_id, submit_time] : pending_submissions_) {
            if(current_time - submit_time >= pending_threshold_) {
                ++count;
            }
        }
        return count;
    }

    const std::unordered_map<OrderId, TimePoint>& get_pending_submissions() const { return pending_submissions_; }

private:
    Duration pending_threshold_;
    std::unordered_map<OrderId, TimePoint> pending_submissions_;
};
