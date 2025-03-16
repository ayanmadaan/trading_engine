#pragma once

class ValueObservabilityChecker {
public:
    explicit ValueObservabilityChecker(double min_observable_value) noexcept
        : min_observable_value_{min_observable_value} {}

    [[nodiscard]] bool is_value_observable(double value) const noexcept {
        return std::abs(value) >= min_observable_value_;
    }

private:
    double min_observable_value_;
};
