#pragma once

#include "../infra/book.hpp"
#include "../utils/helper.hpp"
#include "format.h"
#include "logging.h"
#include <chrono>

class IBookChecker {
public:
    virtual ~IBookChecker() = default;
    [[nodiscard]] virtual bool check(const Book& book) const = 0;
};

class BookFreshnessChecker : public IBookChecker {
public:
    explicit BookFreshnessChecker(uint64_t stale_threshold_ns)
        : stale_threshold_ns_{stale_threshold_ns} {}

    [[nodiscard]] bool check(const Book& book) const override {
        return (get_current_time() - book.m_timestamp) <= stale_threshold_ns_;
    }

private:
    [[nodiscard]] static uint64_t get_current_time() { return helper::get_current_timestamp_ns(); }

    const uint64_t stale_threshold_ns_;
};

class BookSpreadChecker : public IBookChecker {
public:
    explicit BookSpreadChecker(double spread_threshold)
        : spread_threshold_{spread_threshold} {}

    [[nodiscard]] bool check(const Book& book) const override { return book.getSpread() <= spread_threshold_; }

private:
    const double spread_threshold_;
};
