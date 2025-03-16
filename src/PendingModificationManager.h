#pragma once

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

template<typename Clock = std::chrono::system_clock>
class PendingModificationManager {
public:
    using TimePoint = typename Clock::time_point;
    using Duration = typename Clock::duration;
    using OrderId = uint64_t;

    explicit PendingModificationManager(Duration pending_threshold)
        : pending_threshold_(pending_threshold) {}

    bool add(OrderId order_id) { return pending_modifications_.try_emplace(order_id, Clock::now()).second; }

    bool remove(OrderId order_id) { return pending_modifications_.erase(order_id) > 0; }

    [[nodiscard]] bool has(OrderId order_id) const { return pending_modifications_.count(order_id) > 0; }

    std::vector<OrderId> get_outdated_modifications() const {
        auto current_time = Clock::now();
        std::vector<OrderId> outdated;

        for(const auto& [order_id, modify_time] : pending_modifications_) {
            if(current_time - modify_time >= pending_threshold_) {
                outdated.push_back(order_id);
            }
        }
        return outdated;
    }

    size_t get_outdated_pending_count() const {
        auto current_time = Clock::now();
        size_t count = 0;

        for(const auto& [order_id, modify_time] : pending_modifications_) {
            if(current_time - modify_time >= pending_threshold_) {
                ++count;
            }
        }
        return count;
    }

private:
    Duration pending_threshold_;
    std::unordered_map<OrderId, TimePoint> pending_modifications_;
};
