#pragma once

#include "CooldownTimer.h"
#include <chrono>
#include <cstdint>
#include <unordered_set>
#include <vector>

class PendingCancellationManager {
public:
    using OrderId = uint64_t;

    explicit PendingCancellationManager(CooldownTimer::Duration resend_interval) : resend_timer_(resend_interval) {}

    bool add(OrderId order_id) { return pending_cancellations_.insert(order_id).second; }

    bool remove(OrderId order_id) { return pending_cancellations_.erase(order_id) > 0; }

    // [[nodiscard]] bool has(OrderId order_id) const { return pending_cancellations_.contains(order_id); }
    [[nodiscard]] bool has(OrderId order_id) const { return pending_cancellations_.count(order_id) > 0; };

    std::vector<OrderId> get_pending_cancellations_to_resend() {
        if(resend_timer_.is_in_cooldown()) {
            return {};
        }

        std::vector<OrderId> to_resend(pending_cancellations_.begin(), pending_cancellations_.end());
        resend_timer_.start_cooldown();
        return to_resend;
    }

    size_t get_outdated_pending_count() const { return pending_cancellations_.size(); }

private:
    std::unordered_set<OrderId> pending_cancellations_;
    CooldownTimer resend_timer_;
};
