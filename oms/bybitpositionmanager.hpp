
#pragma once

#include "../src/Side.h"
#include "../utils/pinthreads.hpp"
#include "bybitreconciliationmanager.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <curl/curl.h>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

class ByBitPositionManager {
public:
    using ReconFailureCallback = std::function<void(ReconStatus)>;
    explicit ByBitPositionManager(bool trading_mode = false,
                                  double max_position = 0.,
                                  double base_position = 0.,
                                  double tickSize = 0.1,
                                  double tolerableThreshold = 0.5,
                                  uint32_t maxMismatchCount = 3,
                                  uint32_t maxFailQueryCount = 5,
                                  uint32_t retryIntervalOnFailure = 2000,
                                  uint32_t normalReconInterval = 5000,
                                  uint32_t retryIntervalOnMismatch = 2000,
                                  const std::string category = "linear",
                                  const std::string instrument = "DOGEUSDT",
                                  const std::string apiKey = "",
                                  const std::string apiSecret = "")
        : m_maxPosition{max_position}
        , m_basePosition{base_position}
        , m_tickSize{tickSize}
        , m_tolerableThreshold{tolerableThreshold}
        , m_maxMismatchCount{maxMismatchCount}
        , m_maxFailQueryCount{maxFailQueryCount}
        , m_retryIntervalOnFailure{retryIntervalOnFailure}
        , m_normalReconInterval{normalReconInterval}
        , m_retryIntervalOnMismatch{retryIntervalOnMismatch}
        , m_running{false}
        , m_reconManager(trading_mode,
                         tickSize,
                         tolerableThreshold,
                         maxMismatchCount,
                         maxFailQueryCount,
                         category,
                         instrument,
                         retryIntervalOnFailure,
                         normalReconInterval,
                         retryIntervalOnMismatch,
                         apiKey,
                         apiSecret) {
        // Start the reconciliation thread
        // m_reconThread = std::thread(&ByBitPositionManager::reconciliationLoop, this);
        posReconWarmup();
    }

    ~ByBitPositionManager() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_running) {
            stop();
        }
    }

    void start(ReconFailureCallback reconCallback = nullptr) {
        if(m_running) {
            return;
        }
        if(m_reconThread.joinable()) {
            m_reconThread.join();
        }

        m_running = true;
        m_reconStatus = ReconStatus::NoGap;
        m_reconCallback = reconCallback;
        m_reconThread = std::thread(&ByBitPositionManager::reconciliationLoop, this);
        LoggerSingleton::get().infra().info("bybit position reconciliation started");
    }

    void pinThread(int core_id) { setThreadAffinity(m_reconThread, core_id); }

    // Stop the reconciliation process
    void stop() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if(!m_running) {
                return; // Already stopped
            }
            m_running = false;
        }
        m_reconCV.notify_all(); // Wake up ALL waiting threads

        // Join outside the lock
        if(m_reconThread.joinable()) {
            m_reconThread.join();
        }
    }

    void posReconWarmup() {
        auto res = m_reconManager.fetch_pos();
        warmup = res.first;
        if(!res.first) {
            LoggerSingleton::get().infra().error("action=initialize_position result=fail exchange=bybit");
            return;
        } else {
            LoggerSingleton::get().infra().info(
                "action=initialize_position result=pass exchange=bybit trading_position=",
                m_currentPosition,
                " position_from_exch=",
                res.second,
                " base_position=",
                m_basePosition);
            m_currentPosition.store(res.second, std::memory_order_relaxed);
        }
    }

    bool isPosReconWarmedUp() const noexcept { return this->warmup; }

    void reconciliationLoop() {
        while(m_running) {
            try {
                std::unique_lock<std::mutex> lock(m_mutex);

                // Wait until next reconciliation time or when explicitly triggered//strategy.recon()
                m_reconCV.wait_until(lock, m_nextReconTime, [this]() {
                    return !m_running || std::chrono::system_clock::now() >= m_nextReconTime;
                });

                if(!m_running) break;

                lock.unlock();

                // Query exchange position through ReconciliationManager
                auto result =
                    m_reconManager.reconcile(m_currentPosition.load(std::memory_order_relaxed), m_reconStatus);
                if(get<0>(result)) {
                    if(get<1>(result) == m_normalReconInterval) {
                        m_nextReconTime += std::chrono::milliseconds(m_normalReconInterval);
                        m_currentPosition.store(get<2>(result), std::memory_order_relaxed);

                    } else if(get<1>(result) == m_retryIntervalOnFailure) {
                        m_nextReconTime += std::chrono::milliseconds(m_retryIntervalOnFailure);
                    } else if(get<1>(result) == m_retryIntervalOnMismatch) {
                        m_nextReconTime += std::chrono::milliseconds(m_retryIntervalOnMismatch);
                    }
                }

                if(m_reconPromise.has_value()) {
                    m_reconPromise.value().set_value(m_reconStatus);
                    m_reconPromise.reset(); // Reset the promise after resolving it
                }

                if(m_reconCallback) {
                    m_reconCallback(m_reconStatus);
                }
                if(m_reconStatus == ReconStatus::FailedQuery || m_reconStatus == ReconStatus::IntolerableGap) {
                    break;
                }

            } catch(const std::exception& e) {
                LoggerSingleton::get().infra().error("error in reconciliation loop: ", e.what());
                // Set retry interval on error
                std::lock_guard<std::mutex> lock(m_mutex);
                m_nextReconTime =
                    std::chrono::system_clock::now() + std::chrono::milliseconds(m_retryIntervalOnFailure);
                if(m_reconPromise.has_value()) {
                    m_reconPromise.value().set_value(ReconStatus::FailedQuery);
                    m_reconPromise.reset(); // Reset the promise after resolving it
                }
            }
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
        if(m_reconPromise.has_value()) {
            m_reconPromise.value().set_value(ReconStatus::FailedQuery);
            m_reconPromise.reset();
        }
    }

    std::future<ReconStatus> recon() {
        std::promise<ReconStatus> promise;
        auto future = promise.get_future();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if(!m_running) {
                promise.set_value(m_reconStatus); // If not running, return FailedQuery
                return future;
            }
            m_reconCV.notify_one();
            m_reconPromise = std::move(promise);
        }
        return future;
    }

    // Get the internal (cached) position
    [[nodiscard]]
    double get_position() const noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_currentPosition - m_basePosition;
    }

    [[nodiscard]] double get_max_position() const noexcept { return m_maxPosition; }

    template<Side::Type SideType>
    [[nodiscard]] bool is_max_position() const {
        const double position = get_position();
        if constexpr(SideType == Side::Type::Ask) {
            return -position >= m_maxPosition; // short position
        } else {
            return position >= m_maxPosition; // long position
        }
    }

    [[nodiscard]] bool is_max_position(Side::Type side) const {
        const double position = get_position();
        return (side == Side::Type::Ask) ? (-position >= m_maxPosition) : (position >= m_maxPosition);
    }

    // Update position based on fills
    void update_position_by_fillsz(double fill_sz, bool side) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        updateCurrentPosition(fill_sz, side);
    }

private:
    double m_maxPosition;
    double m_basePosition;
    bool warmup;
    std::atomic<double> m_currentPosition{};
    mutable std::mutex m_mutex;

    ReconFailureCallback m_reconCallback = nullptr;

    ByBitReconciliationManager m_reconManager;

    // Reconciliation parameters
    double m_tickSize;
    double m_tolerableThreshold;
    uint32_t m_maxMismatchCount;
    uint32_t m_maxFailQueryCount;
    uint32_t m_retryIntervalOnFailure;
    uint32_t m_normalReconInterval;
    uint32_t m_retryIntervalOnMismatch;

    // Counters and timers
    std::atomic<int> m_mismatchCounter{0};
    std::atomic<int> m_failQueryCounter{0};
    std::chrono::system_clock::time_point m_nextReconTime{std::chrono::system_clock::now()};

    // Thread and synchronization
    std::thread m_reconThread;
    std::condition_variable m_reconCV;
    std::atomic<bool> m_running;
    // std::atomic<bool> m_reconStatus{true};
    ReconStatus m_reconStatus;
    std::optional<std::promise<ReconStatus>> m_reconPromise;

    // Update internal position
    void updateCurrentPosition(double fill_sz, bool side) noexcept {
        if(side) { // BUY
            this->m_currentPosition += fill_sz;
        } else { // SELL
            this->m_currentPosition -= fill_sz;
        }
    }
};
