#pragma once

#include "../src/format.h"
#include "../utils/logger.hpp"
#include "bybitclient.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
class ByBitReconciliationManager {
public:
    // Constructor with dynamic configuration
    explicit ByBitReconciliationManager(const bool tradingMode,
                                        const double tickSize,
                                        const double tolerableThreshold,
                                        const uint32_t maxMismatchCount,
                                        const uint32_t maxFailQueryCount,
                                        const std::string category,
                                        const std::string instrument,
                                        const uint32_t retryIntervalOnFailure,
                                        const uint32_t normalReconInterval,
                                        const uint32_t retryIntervalOnMismatch,
                                        const std::string bybitApiKey,
                                        const std::string bybitApiSecret)
        : m_tickSize{tickSize}, m_tolerableThreshold{tolerableThreshold}, m_category{category},
          m_instrument(instrument), m_maxMismatchCount{maxMismatchCount}, m_maxFailQueryCount{maxFailQueryCount},
          m_retryIntervalOnFailure{retryIntervalOnFailure}, m_normalReconInterval{normalReconInterval},
          m_retryIntervalOnMismatch{retryIntervalOnMismatch}, m_bybitClient(tradingMode, bybitApiKey, bybitApiSecret) {}

    // Perform reconciliation
    std::tuple<bool, uint32_t, double> reconcile(double internalPosition, ReconStatus& m_reconStatus) {
        auto res = m_bybitClient.fetch_position_impl(m_category, m_instrument);
        double exchangePosition = res.second;
        if(!res.first) {
            // CURL request failed
            m_reconTryCounter++;
            m_reconStatus = ReconStatus::NoGap;

            if(m_reconTryCounter >= m_maxFailQueryCount) {
                LoggerSingleton::get().infra().error(
                    f("action", "position_recon") + " " + f("exchange", "bybit") + " " + f("result", "fail") + " " +
                    f("reason", "curl_failed_exceeds_max_retry") + f("retry_count", m_reconTryCounter) + " " +
                    f("max_retry_count", m_maxFailQueryCount));
                m_reconStatus = ReconStatus::FailedQuery;
                return {false, 0, 0};
            }
            LoggerSingleton::get().infra().warning(f("action", "position_recon") + " " + f("exchange", "bybit") + " " +
                                                   f("result", "fail") + " " + f("reason", "curl_failed") +
                                                   f("retry_count", m_reconTryCounter) + " " +
                                                   f("max_retry_count", m_maxFailQueryCount));
            return {true, m_retryIntervalOnFailure, 0};
        }

        m_reconTryCounter = 0;
        double prev_gap = gap;
        gap = std::abs(exchangePosition - internalPosition);

        if(gap < m_tickSize) {
            m_mismatchCounter = 0;
            m_reconStatus = ReconStatus::NoGap;
            return {true, m_normalReconInterval, exchangePosition};
        } else if(gap < m_tolerableThreshold) {
            if(prev_gap == gap) {
                m_mismatchCounter++;
                m_reconTryCounter = 0;
            } else {
                m_mismatchCounter = 1;
                m_reconTryCounter += 1;
            }

            m_reconStatus = ReconStatus::NoGap;
            if(m_mismatchCounter >= m_maxMismatchCount) {
                LoggerSingleton::get().infra().warning(
                    f("action", "position_recon") + " " + f("exchange", "bybit") + " " + f("result", "fail") + " " +
                    f("reason", "confirmed_gap_within_threshold") + " " + f("gap", gap) + " " +
                    f("exchange_position", exchangePosition) + " " + f("internal_position", internalPosition) + " " +
                    f("tolerable_threshold", m_tolerableThreshold) + " " + f("gap_within_threshold", true) + " " +
                    f("mismatch_count", m_mismatchCounter) + f("max_mismatch_count", m_maxMismatchCount));
                m_reconStatus = ReconStatus::TolerableGap;
                return {true, m_normalReconInterval, exchangePosition};
            }
            if(m_reconTryCounter >= m_maxFailQueryCount) {
                LoggerSingleton::get().infra().error(
                    f("action", "position_recon") + " " + f("exchange", "bybit") + " " + f("result", "fail") + " " +
                    f("reason", "failed_to_confirm_gap ") + " " + f("gap", gap) + " " +
                    f("exchange_position", exchangePosition) + " " + f("internal_position", internalPosition) + " " +
                    f("retry_count", m_reconTryCounter) + " " + f("max_retry_count", m_maxFailQueryCount));
                m_reconStatus = ReconStatus::UndeterminedGap;
                return {false, 0, exchangePosition};
            };
            LoggerSingleton::get().infra().info(
                f("action", "position_recon") + " " + f("exchange", "bybit") + " " + f("result", "pass") + " " +
                f("gap", gap) + " " + f("exchange_position", exchangePosition) + " " +
                f("internal_position", internalPosition) + " " + f("tolerable_threshold", m_tolerableThreshold) + "  " +
                f("gap_within_threshold", true) + " " + f("mismatch_count", m_mismatchCounter) + " " +
                f("max_mismatch_count", m_maxMismatchCount));
            return {true, m_retryIntervalOnMismatch, exchangePosition};
        } else {
            if(prev_gap == gap) {
                m_mismatchCounter++;
                m_reconTryCounter = 0;
            } else {
                m_mismatchCounter = 1;
                m_reconTryCounter += 1;
            }
            m_reconStatus = ReconStatus::NoGap;
            if(m_mismatchCounter >= m_maxMismatchCount) {
                LoggerSingleton::get().infra().error(
                    f("action", "position_recon") + " " + f("exchange", "bybit") + " " + f("result", "fail") + " " +
                    f("reason", "confirmed_gap_exceeds_threshold") + " " + f("gap", gap) + " " +
                    f("exchange_position", exchangePosition) + " " + f("internal_position", internalPosition) + " " +
                    f("tolerable_threshold", m_tolerableThreshold) + " " + f("gap_within_threshold", false) + " " +
                    f("mismatch_count", m_mismatchCounter) + f("max_mismatch_count", m_maxMismatchCount));
                m_reconStatus = ReconStatus::IntolerableGap;
                return {false, 0, exchangePosition};
            }
            if(m_reconTryCounter >= m_maxFailQueryCount) {
                LoggerSingleton::get().infra().error(
                    f("action", "position_recon") + " " + f("exchange", "bybit") + " " + f("result", "fail") + " " +
                    f("reason", "failed_to_confirm_gap ") + " " + f("gap", gap) + " " +
                    f("exchange_position", exchangePosition) + " " + f("internal_position", internalPosition) + " " +
                    f("retry_count", m_reconTryCounter) + " " + f("max_retry_count", m_maxFailQueryCount));
                m_reconStatus = ReconStatus::UndeterminedGap;
                return {false, 0, exchangePosition};
            }
            LoggerSingleton::get().infra().info(
                f("action", "position_recon") + " " + f("exchange", "bybit") + " " + f("result", "pass") + " " +
                f("gap", gap) + " " + f("exchange_position", exchangePosition) + " " +
                f("internal_position", internalPosition) + " " + f("tolerable_threshold", m_tolerableThreshold) + "  " +
                f("gap_within_threshold", false) + " " + f("mismatch_count", m_mismatchCounter) + " " +
                f("max_mismatch_count", m_maxMismatchCount));
            return {true, m_retryIntervalOnMismatch, exchangePosition};
        }
    }

    std::pair<bool, double> fetch_pos() { return m_bybitClient.fetch_position_impl(m_category, m_instrument); }

    // Check if it's time to perform reconciliation
    bool is_time_for_recon() const { return std::chrono::system_clock::now() >= m_nextReconTime; }

private:
    double m_tickSize; // Minimum gap to consider as a mismatch
    double m_tolerableThreshold; // Maximum gap to tolerate without stopping trading
    double gap;
    uint32_t m_maxMismatchCount; // Maximum allowed consecutive mismatches
    uint32_t m_maxFailQueryCount; // Maximum allowed failed queries

    std::string m_category;
    std::string m_instrument;

    uint32_t m_retryIntervalOnFailure; // Retry interval after a failed query
    uint32_t m_normalReconInterval; // Normal reconciliation interval
    uint32_t m_retryIntervalOnMismatch; // Retry interval after a mismatch

    std::atomic<int> m_mismatchCounter{0}; // Counter for consecutive mismatches
    std::atomic<int> m_reconTryCounter{0}; // Counter for failed queries
    std::chrono::system_clock::time_point m_nextReconTime{std::chrono::system_clock::now()}; // Next reconciliation time
    BybitClient m_bybitClient;
};
