#pragma once

#include "logging.h"

template<typename PnlProvider>
class StopLoss {
public:
    template<typename TPnlProvider = PnlProvider>
    explicit StopLoss(double stop_loss_threshold, const TPnlProvider& provider)
        : stop_loss_threshold_(stop_loss_threshold)
        , pnl_provider_(provider) {}

    [[nodiscard]] bool is_stop_loss() const {
        const auto pnl = pnl_provider_.get_total_pnl_with_fee();
        const bool result = pnl <= stop_loss_threshold_;

        [[unlikely]] if(result) {
            log_action_fail<LogLevel::WARNING>(
                "check_stop_loss", f("total_pnl_with_fee", pnl), f("stop_loss_threshold", stop_loss_threshold_));
        }

        LOG_STRATEGY_DEBUG([&]() {
            return "[StopLoss] " + f("action", "check_stop_loss") + " " + f("total_pnl_with_fee", pnl) + " " +
                   f("stop_loss_threshold", stop_loss_threshold_) + " " + f("is_stop_loss", result);
        }());

        return result;
    }

    [[nodiscard]] double get_stop_loss_threshold() const { return stop_loss_threshold_; }

private:
    double stop_loss_threshold_; // Negative number represents loss percentage
    const PnlProvider& pnl_provider_;
};
