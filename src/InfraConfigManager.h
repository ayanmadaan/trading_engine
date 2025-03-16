#pragma once

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

struct InfraConfig {
    const fs::path strategy_config_path;
    const fs::path strategy_log_dir;

    InfraConfig(fs::path strat_path, fs::path strat_log_dir)
        :strategy_config_path(std::move(strat_path)), strategy_log_dir(std::move(strat_log_dir)) {
        validate_paths_not_empty(strategy_config_path, strategy_log_dir);
    }

private:
    static void validate_paths_not_empty(const fs::path& strat_path, const fs::path& strat_log_dir) {
        if(strat_path.empty() || strat_log_dir.empty()) {
            std::string error_msg;
            if(strat_path.empty() && strat_log_dir.empty()) {
                error_msg = "Both strategy_config_path and strategy_log_dir cannot be empty";
            } else if (strat_path.empty()){
                error_msg = "strategy_config_path cannot be empty";
            } else if (strat_log_dir.empty()) {
                error_msg = "strategy_log_dir cannot be empty";
            }
            throw std::invalid_argument(error_msg);
        }
    }
};

class InfraConfigManager {
public:
    class ConfigError : public std::runtime_error {
    public:
        explicit ConfigError(const std::string& msg) : std::runtime_error(msg) {}
    };

    explicit InfraConfigManager(const fs::path& config_path)
        : config_path_(config_path), config_(load_config(config_path_)) {}

    InfraConfigManager(const InfraConfigManager&) = delete;
    InfraConfigManager& operator=(const InfraConfigManager&) = delete;
    InfraConfigManager(InfraConfigManager&&) noexcept = default;
    InfraConfigManager& operator=(InfraConfigManager&&) noexcept = default;

    [[nodiscard]] const InfraConfig& get_config() const { return config_; }
    [[nodiscard]] const fs::path& get_config_path() const { return config_path_; }

private:
    const fs::path config_path_;
    InfraConfig config_;

    static InfraConfig load_config(const fs::path& config_path) {
        std::ifstream config_file(config_path);
        if(!config_file) {
            throw ConfigError("Failed to open config file: " + config_path.string());
        }

        try {
            nlohmann::json config_json;
            config_file >> config_json;

            fs::path strategy_config_path = config_json.value("strategy_config_path", "");
            fs::path strategy_log_dir = config_json.value("strategy_log_dir", "");

            if(!strategy_config_path.empty()) {
                strategy_config_path = fs::absolute(strategy_config_path);
            }
            if (!strategy_log_dir.empty()) {
                strategy_log_dir = fs::absolute(strategy_log_dir);
            }

            InfraConfig config(std::move(strategy_config_path), std::move(strategy_log_dir));

            validate_paths(config);
            return config;

        } catch(const nlohmann::json::exception& e) {
            throw ConfigError(std::string("JSON parsing error: ") + e.what());
        } catch(const std::invalid_argument& e) {
            throw ConfigError("Missing required fields in config file");
        } catch(const std::exception& e) {
            throw ConfigError(std::string("Config parsing error: ") + e.what());
        }
    }

    static void validate_paths(const InfraConfig& config) {
        if(!fs::exists(config.strategy_config_path)) {
            throw ConfigError("Strategy config file not found: " + config.strategy_config_path.string());
        }
        if(!fs::is_regular_file(config.strategy_config_path)) {
            throw ConfigError("Strategy config is not a file: " + config.strategy_config_path.string());
        }
    }
};
