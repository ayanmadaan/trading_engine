#pragma once

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

/**
 * @brief Custom exception class for ArgumentParser errors
 */
class ArgumentParserError : public std::runtime_error {
public:
    /**
     * @brief Constructs a new ArgumentParserError
     * @param message The error message
     */
    explicit ArgumentParserError(const std::string& message) : std::runtime_error(message) {}
};

/**
 * @brief Parser for command-line arguments that handles config file path validation
 */
class ArgumentParser {
public:
    /**
     * @brief Constructs and validates an ArgumentParser
     *
     * @param argc Number of command-line arguments
     * @param argv Array of command-line arguments
     * @throws ArgumentParserError if:
     *         - exactly one argument is not provided
     *         - the path format is invalid
     *         - the config file doesn't exist
     *         - the path is not a regular file
     *         - the config file is not readable
     */
    ArgumentParser(int argc, char** argv) {
        if(argc != 2) {
            throw ArgumentParserError("Expected exactly one argument (config path), got " + std::to_string(argc - 1));
        }

        try {
            config_path_ = std::filesystem::absolute(std::filesystem::path(argv[1]));
        } catch(const std::filesystem::filesystem_error& e) {
            throw ArgumentParserError(std::string("Invalid path format: ") + e.what());
        }

        validate_config_path();
    }

    /**
     * @brief Gets the validated config file path
     * @return const std::filesystem::path& Absolute path to the config file
     */
    [[nodiscard]] const std::filesystem::path& get_config_path() const { return config_path_; }

private:
    std::filesystem::path config_path_;

    /**
     * @brief Validates that the config file exists, is a regular file, and is readable
     * @throws ArgumentParserError if any validation check fails
     */
    void validate_config_path() const {
        if(!std::filesystem::exists(config_path_)) {
            throw ArgumentParserError("Config file does not exist: " + config_path_.string());
        }

        if(!std::filesystem::is_regular_file(config_path_)) {
            throw ArgumentParserError("Path is not a regular file: " + config_path_.string());
        }

        const std::ifstream file(config_path_);
        if(!file.good()) {
            throw ArgumentParserError("Config file is not readable: " + config_path_.string());
        }
    }
};
