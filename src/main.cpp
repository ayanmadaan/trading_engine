#include "../infra/pinthreads.hpp"
#include "../lib/json.hpp"
#include "../utils/logger.hpp"
#include "ArgumentParser.h"
#include "Configuration.h"
#include "InfraConfigManager.h"
#include "Signal.h"
#include "strategy.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
namespace {

static Configuration load_strategy_config(const fs::path& config_path) {
    LoggerSingleton::get().infra().info("reading configuration file from path: " + config_path.string());

    auto config = Configuration::from_file(config_path);
    if(!config) {
        throw std::runtime_error("failed to load strategy configuration");
    }

    config->set("config_path", "\"" + config_path.string() + "\"");

    Configuration config_copy = config->deep_copy();
    config_copy.remove_key("api_key");
    config_copy.remove_key("api_secret");
    config_copy.remove_key("api_passphrase");

    std::string sanitized_yaml = config_copy.dump_compact();
    LoggerSingleton::get().infra().info("configuration content: " + sanitized_yaml);

    return *config;
}

static void setup_signal_handler(Signal& signal) {
    if(!signal.setupSignalHandlers()) {
        throw std::runtime_error("failed to set up signal handlers");
    }
    signal.start();
}

} // namespace

int main(int argc, char** argv) {
    try {
        ArgumentParser parser(argc, argv);
        InfraConfigManager config_manager(parser.get_config_path());
        LoggerSingleton::initialize(config_manager.get_config().strategy_log_dir.generic_string(),
                                    config_manager.get_config().strategy_config_path.generic_string());
        Configuration strategy_config = load_strategy_config(config_manager.get_config().strategy_config_path);

        Signal signal;
        setup_signal_handler(signal);

        int strategy_timeout = strategy_config.child("trading_control").get<int>("strategy_ready_timeout_seconds");
        Strategy strategy(strategy_config);
        std::chrono::seconds strategy_timeout_duration(strategy_timeout);
        signal.handleStrategy<Strategy>(strategy, strategy_timeout_duration);
        return 0;
    } catch(const ArgumentParserError& e) {
        LoggerSingleton::get().strategy().error("Argument Error: " + std::string(e.what()));
        std::cerr << "Argument Error: " << e.what() << std::endl;
        return 1;
    } catch(const InfraConfigManager::ConfigError& e) {
        LoggerSingleton::get().strategy().error("Configuration Error: " + std::string(e.what()));
        std::cerr << "Configuration Error: " << e.what() << std::endl;
        return 1;
    } catch(const std::exception& e) {
        LoggerSingleton::get().strategy().error("Unexpected Error: " + std::string(e.what()));
        std::cerr << "Unexpected Error: " << e.what() << std::endl;
        return 1;
    }
}
