#pragma once

#include <chrono>
#include <mutex>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

#include "logging.h"
class TimingStats {
public:
    void add_sample(int64_t duration_us) {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.push_back(duration_us);
        if(samples_.size() > max_samples_) {
            samples_.erase(samples_.begin());
        }
    }

    double average_us() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if(samples_.empty()) return 0.0;

        int64_t sum = std::accumulate(samples_.begin(), samples_.end(), 0LL);
        return static_cast<double>(sum) / samples_.size();
    }

    size_t sample_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return samples_.size();
    }

    void set_max_samples(size_t max) { max_samples_ = max; }

private:
    mutable std::mutex mutex_;
    std::vector<int64_t> samples_;
    size_t max_samples_ = 1000;
};

class TimerRegistry {
public:
    static TimerRegistry& instance() {
        static TimerRegistry registry;
        return registry;
    }

    TimingStats& get_stats(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_[name];
    }

    void log_all_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        for(const auto& [name, stats] : stats_) {
            std::stringstream ss;
            ss << "[timing] " << name << " avg=" << std::fixed << std::setprecision(6) << stats.average_us()
               << " us samples=" << stats.sample_count();

            LoggerSingleton::get().strategy().info(ss.str());
        }
    }

private:
    TimerRegistry() = default;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, TimingStats> stats_;
};

class ScopedTimer {
public:
    ScopedTimer(const std::string& name)
        : name_(name)
        , start_(std::chrono::high_resolution_clock::now()) {}

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
        TimerRegistry::instance().get_stats(name_).add_sample(duration);
    }

private:
    std::string name_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};

#define TIME_FUNCTION() ScopedTimer timer(__FUNCTION__)
#define TIME_BLOCK(name) ScopedTimer timer(name)
