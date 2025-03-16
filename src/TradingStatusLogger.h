#include "../utils/logger.hpp"
#include "format.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <utility>

class TradingStatusLogger {
public:
    using StatusCallback = std::function<nlohmann::json()>;

    template<typename Duration = std::chrono::milliseconds, typename Callback = std::function<nlohmann::json()>>
    TradingStatusLogger(std::filesystem::path file_path, Duration interval, Callback callback)
        : file_path_(std::move(file_path)), interval_(std::chrono::duration_cast<std::chrono::milliseconds>(interval)),
          status_callback_(std::move(callback)), running_(false) {

        if(!validate_path(file_path_)) {
            throw std::runtime_error("Invalid log file path: " + file_path_.string());
        }
    }

    void dumpFinalStatus() {
        if (!status_callback_) {
            return;
        }

        try {
            const nlohmann::json status_json = status_callback_();
            std::ofstream ofs(file_path_, std::ios::out | std::ios::trunc);
            if(!ofs.is_open()) {
                throw std::runtime_error("Failed to open file: " + file_path_.string());
            }
            ofs << status_json.dump(4);
            ofs.close();
            if(ofs.fail()) {
                throw std::runtime_error("Failed to write to file: " + file_path_.string());
            }
        } catch(const nlohmann::json::exception& e) {
            LoggerSingleton::get().strategy().error(f("action", "final_status_dump") + " " +
                                                    f("reason", "json_error") + " " + f("error", e.what()));
        } catch(const std::runtime_error& e) {
            LoggerSingleton::get().strategy().error(f("action", "final_status_dump") + " " +
                                                    f("reason", "file_error") + " " + f("error", e.what()));
        } catch(const std::exception& e) {
            LoggerSingleton::get().strategy().error(f("action", "final_status_dump") + " " +
                                                    f("reason", "std_error") + " " + f("error", e.what()));
        } catch(...) {
            LoggerSingleton::get().strategy().error(f("action", "final_status_dump") + " " +
                                                    f("reason", "unknown_error"));
        }
    }

    ~TradingStatusLogger() {
        // if(status_callback_) {
        //     try {
        //         const nlohmann::json status_json = status_callback_();
        //         std::ofstream ofs(file_path_, std::ios::out | std::ios::trunc);
        //         if(!ofs.is_open()) {
        //             throw std::runtime_error("Failed to open file: " + file_path_.string());
        //         }
        //         ofs << status_json.dump(4);
        //         ofs.close();
        //         if(ofs.fail()) {
        //             throw std::runtime_error("Failed to write to file: " + file_path_.string());
        //         }
        //     } catch(const nlohmann::json::exception& e) {
        //         LoggerSingleton::get().strategy().error(f("action", "final_status_dump") + " " +
        //                                                 f("reason", "json_error") + " " + f("error", e.what()));
        //     } catch(const std::runtime_error& e) {
        //         LoggerSingleton::get().strategy().error(f("action", "final_status_dump") + " " +
        //                                                 f("reason", "file_error") + " " + f("error", e.what()));
        //     } catch(const std::exception& e) {
        //         LoggerSingleton::get().strategy().error(f("action", "final_status_dump") + " " +
        //                                                 f("reason", "std_error") + " " + f("error", e.what()));
        //     } catch(...) {
        //         LoggerSingleton::get().strategy().error(f("action", "final_status_dump") + " " +
        //                                                 f("reason", "unknown_error"));
        //     }
        // }
        stop();
    }

    void start() {
        running_ = true;
        logger_thread_ = std::thread(&TradingStatusLogger::run, this);
    }

    void stop() {
        running_ = false;
        if(logger_thread_.joinable()) {
            logger_thread_.join();
        }
    }

    TradingStatusLogger(const TradingStatusLogger&) = delete;

    TradingStatusLogger& operator=(const TradingStatusLogger&) = delete;

    TradingStatusLogger(TradingStatusLogger&& other) noexcept
        : file_path_(std::move(other.file_path_)), interval_(other.interval_),
          status_callback_(std::move(other.status_callback_)), running_(other.running_.load()) {
        other.running_.store(false);
    }

    TradingStatusLogger& operator=(TradingStatusLogger&& other) noexcept {
        if(this != &other) {
            file_path_ = std::move(other.file_path_);
            interval_ = other.interval_;
            status_callback_ = std::move(other.status_callback_);
            running_.store(other.running_.load());
            other.running_.store(false);
        }
        return *this;
    }

private:
    bool validate_path(const std::filesystem::path& path) {
        try {
            auto parent_path = path.parent_path();
            if(!parent_path.empty() && !std::filesystem::exists(parent_path)) {
                std::filesystem::create_directories(parent_path);
            }

            std::ofstream test_file(path);
            if(!test_file) {
                return false;
            }
            test_file.close();

            return true;
        } catch(const std::filesystem::filesystem_error& e) {
            return false;
        }
    }

    void run() {
        while(running_) {
            const nlohmann::json status_json = status_callback_();
            std::ofstream ofs(file_path_, std::ios::out | std::ios::trunc);
            if(ofs.is_open()) {
                ofs << status_json.dump(4);
                ofs.close();
            }
            std::this_thread::sleep_for(interval_);
        }
    }

    std::filesystem::path file_path_;
    std::chrono::milliseconds interval_;
    StatusCallback status_callback_;
    std::thread logger_thread_;
    std::atomic<bool> running_;
};
