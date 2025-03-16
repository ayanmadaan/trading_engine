#pragma once

#include <c4/yml/std/string.hpp>
#include <c4/yml/tree.hpp>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include <sstream>
#include <stdexcept>
#include <string>

/**
 * @class Configuration
 * @brief Provides tree-like navigation of a YAML document using RapidYAML.
 *
 * This class maintains ownership of the underlying character buffer for
 * parse_in_place, ensuring the data remains valid during the lifetime of
 * the Configuration object and any of its children.
 */
class Configuration {
public:
    /**
     * @brief Construct a Configuration object from a YAML file.
     * @param file_path The path to a YAML file.
     * @return A fully parsed Configuration object that owns the parsed data.
     * @throws std::runtime_error if the file cannot be opened or parsed.
     * @example
     *   try {
     *     auto config = Configuration::from_file("config.yaml");
     *     // Use the config...
     *   } catch (const std::runtime_error& e) {
     *     // Handle error...
     *   }
     */
    static std::optional<Configuration> from_file(const std::string& file_path) {
        try {
            std::ifstream ifs(file_path);
            if(!ifs.is_open()) {
                throw std::runtime_error("Could not open YAML file: " + file_path);
            }

            std::stringstream buffer;
            buffer << ifs.rdbuf();
            return from_string(buffer.str()); // Assume this function exists
        } catch(const std::exception& e) {
            return std::nullopt;
        }
    }

    /**
     * @brief Construct a Configuration object from a YAML string.
     * @param yaml_content A string containing valid YAML data.
     * @return A fully parsed Configuration object that owns the parsed data.
     * @throws std::runtime_error if the YAML content fails to parse.
     * @note Uses parse_in_place internally for efficiency. The Configuration object
     *       maintains ownership of the internal buffer to ensure the parsed data
     *       remains valid throughout its lifetime.
     * @example
     *   const std::string yaml = R"(
     *     key: value
     *     list:
     *       - item1
     *       - item2
     *   )";
     *   auto config = Configuration::from_string(yaml);
     */
    static Configuration from_string(const std::string& yaml_content) {
        auto tree = std::make_shared<ryml::Tree>();
        auto buffer = std::make_shared<std::vector<char>>(yaml_content.size() + 1);
        std::memcpy(buffer->data(), yaml_content.data(), yaml_content.size());
        (*buffer)[yaml_content.size()] = '\0';

        ryml::parse_in_place(ryml::to_substr(buffer->data()), tree.get());
        return Configuration(std::move(tree), tree->rootref(), std::move(buffer));
    }

    /**
     * @brief Default constructor creating an invalid (empty) Configuration.
     */
    Configuration() = default;

    /**
     * @brief Check if this Configuration node is valid and accessible.
     * @return true if the node has a valid tree pointer and represents a valid YAML node,
     *         false if either the tree is null or the node reference is invalid.
     * @note A node is considered invalid if it was default-constructed, or if it's
     *       referencing a node that no longer exists in the YAML tree.
     * @example
     *   Configuration config;  // default-constructed, invalid
     *   if (!config.is_valid()) {
     *     // Handle invalid configuration...
     *   }
     */
    [[nodiscard]]
    bool is_valid() const {
        return (tree_ != nullptr) && node_.valid();
    }

    /**
     * @brief Check if this node represents a YAML map/object (key-value pairs).
     * @return true if the node is valid and represents a map,
     *         false if the node is invalid or is not a map.
     * @note A map node in YAML looks like:
     *       key1: value1
     *       key2: value2
     * @example
     *   if (config.is_map()) {
     *     // Access map elements using .get<T>(key)
     *     auto value = config.get<std::string>("key1");
     *   }
     */
    [[nodiscard]]
    bool is_map() const {
        return is_valid() && node_.is_map();
    }

    /**
     * @brief Check if this node represents a YAML sequence/array.
     * @return true if the node is valid and represents a sequence,
     *         false if the node is invalid or is not a sequence.
     * @note A sequence in YAML can be written in two forms:
     *       Block style:
     *       - item1
     *       - item2
     *       Flow style: [item1, item2]
     * @example
     *   if (config.is_seq()) {
     *     // Iterate through sequence elements
     *     for (size_t i = 0; i < config.num_children(); ++i) {
     *       auto item = config.child(i);
     *     }
     *   }
     */
    [[nodiscard]]
    bool is_seq() const {
        return is_valid() && node_.is_seq();
    }

    /**
     * @brief Check if this node represents a YAML scalar value.
     * @return true if the node is valid and contains a scalar value,
     *         false if the node is invalid or not a scalar.
     * @note A scalar is a single value in YAML, which can be:
     *       - A string: "hello" or just hello
     *       - A number: 42 or 3.14
     *       - A boolean: true or false
     *       - A null value: null or ~
     *       - A timestamp: 2024-01-21
     * @example
     *   if (config.is_val()) {
     *     // Access the scalar value
     *     auto str_value = config.as<std::string>();
     *     // or
     *     auto num_value = config.as<int>();
     *   }
     */
    [[nodiscard]]
    bool is_val() const {
        return is_valid() && node_.has_val();
    }

    /**
     * @brief Check if a map node has a child with the specified key.
     * @param key The key to look up.
     * @return true if the key exists in the map, false otherwise.
     * @note This only checks for key existence, not whether it has a value.
     */
    [[nodiscard]]
    bool has_key(const std::string& key) const {
        return is_valid() && node_.is_map() && node_.has_child(ryml::to_csubstr(key));
    }

    /**
     * @brief Check if a map node has a non-null value for the specified key.
     * @param key The key to look up.
     * @return true if the key exists and has a non-null value, false otherwise.
     */
    [[nodiscard]]
    bool has_value(const std::string& key) const {
        if(!has_key(key)) {
            return false;
        }
        auto child_node = node_[ryml::to_csubstr(key)];
        return child_node.has_val() && !child_node.val().empty();
    }

    /**
     * @brief Get the number of child elements in this node.
     * @return size_t The number of direct children:
     *         - For maps: number of key-value pairs
     *         - For sequences: number of elements in the sequence
     *         - For scalar values: 0
     *         - For invalid nodes: 0
     * @note For maps, each key-value pair counts as one child.
     *       For sequences, each element counts as one child.
     * @example
     *   // For a map:
     *   // key1: value1
     *   // key2: value2
     *   config.num_children(); // Returns 2
     *
     *   // For a sequence:
     *   // - item1
     *   // - item2
     *   // - item3
     *   config.num_children(); // Returns 3
     */
    [[nodiscard]]
    size_t num_children() const {
        return is_valid() ? node_.num_children() : 0;
    }

    /**
     * @brief Access a child node by key in a map node.
     * @param key The key to look up in the current map node.
     * @return A Configuration wrapping the child node.
     * @throws std::runtime_error if:
     *         - The current node is invalid ("Cannot access key '<key>' from invalid node")
     *         - The node is not a map ("Cannot access key '<key>' from non-map node")
     *         - The key does not exist ("Key '<key>' not found in map")
     * @example
     *   // For YAML:
     *   // settings:
     *   //   port: 8080
     *   //   host: "localhost"
     *   auto settings = config.child("settings");
     *   auto port = settings.child("port");
     */
    [[nodiscard]]
    Configuration child(const std::string& key) const {
        if(!is_valid()) {
            throw std::runtime_error("Cannot access key '" + key + "' from invalid node");
        }
        if(!node_.is_map()) {
            throw std::runtime_error("Cannot access key '" + key + "' from non-map node");
        }
        if(!node_.has_child(ryml::to_csubstr(key))) {
            throw std::runtime_error("Key '" + key + "' not found in map");
        }

        auto csubstr_key = ryml::to_csubstr(key);
        const ryml::NodeRef child_node = node_[csubstr_key];
        return Configuration(tree_, child_node, buffer_);
    }

    /**
     * @brief Access a child node by index in a sequence node.
     * @param index Zero-based index of the element in the sequence.
     * @return A Configuration wrapping the child node.
     * @throws std::runtime_error if:
     *         - The current node is invalid ("Cannot access index <index> from invalid node")
     *         - The node is not a sequence ("Cannot access index <index> from non-sequence node")
     *         - The index is out of range ("Index <index> out of range, sequence size is <size>")
     * @example
     *   // For YAML:
     *   // items:
     *   //   - name: "item1"
     *   //   - name: "item2"
     *   auto items = config.child("items");
     *   auto first_item = items.child(0);
     */
    [[nodiscard]]
    Configuration child(size_t index) const {
        if(!is_valid()) {
            throw std::runtime_error("Cannot access index " + std::to_string(index) + " from invalid node");
        }
        if(!node_.is_seq()) {
            throw std::runtime_error("Cannot access index " + std::to_string(index) + " from non-sequence node");
        }
        if(index >= node_.num_children()) {
            throw std::runtime_error("Index " + std::to_string(index) + " out of range, sequence size is " +
                                     std::to_string(node_.num_children()));
        }

        const ryml::NodeRef child_node = node_.child(index);
        return {tree_, child_node, buffer_};
    }

    /**
     * @brief Get the parent node of the current node.
     * @return A Configuration object representing the parent node,
     *         or an invalid Configuration if:
     *         - Current node is invalid
     *         - Current node is the root node
     *         - Parent node is invalid
     * @example
     *   // For YAML:
     *   // database:
     *   //   settings:
     *   //     port: 5432
     *
     *   auto port_node = config.child("database").child("settings").child("port");
     *   auto settings = port_node.parent();  // gets the settings node
     *   auto database = settings.parent();   // gets the database node
     */
    [[nodiscard]]
    Configuration parent() const {
        if(!is_valid() || node_.is_root()) {
            return {};
        }
        return {tree_, node_.parent(), buffer_};
    }

    /**
     * @brief Get the root node of the YAML tree.
     * @return A Configuration object representing the root node,
     *         or an invalid Configuration if current node is invalid.
     * @example
     *   // For any deeply nested node
     *   auto deep_node = config.child("a").child("b").child("c");
     *   auto root = deep_node.root();  // gets back to the root node
     */
    [[nodiscard]]
    Configuration root() const {
        if(!is_valid()) {
            return {};
        }
        return {tree_, tree_->rootref(), buffer_};
    }

    /**
     * @brief Convert a node's value to the specified type T.
     * @tparam T The target type:
     *           - std::string
     *           - Integral types (int, long, etc.)
     *           - Floating point types (float, double)
     *           - bool ("true"/"false", "yes"/"no", "1"/"0")
     * @return The node's value converted to type T.
     * @throws std::runtime_error if:
     *         - Node is invalid
     *         - Node has no value
     *         - Value is null (null, Null, NULL, ~) or empty
     *         - Value cannot be converted to type T
     *         - Type T is not supported
     * @example
     *   // YAML:
     *   // port: 8080
     *   // enabled: yes
     *   // ratio: 3.14
     *   // name: "test"
     *
     *   int port = node.as<int>();         // returns 8080
     *   bool enabled = node.as<bool>();    // returns true
     *   double ratio = node.as<double>();  // returns 3.14
     *   std::string name = node.as<std::string>(); // returns "test"
     */
    template<typename T>
    T as() const {
        if(!is_valid()) {
            throw std::runtime_error("Invalid node");
        }

        if(!node_.has_val()) {
            throw std::runtime_error("Node has no value");
        }

        try {
            std::string str_val;
            node_ >> str_val;

            if(str_val.empty() || str_val == "null" || str_val == "Null" || str_val == "NULL" || str_val == "~") {
                throw std::runtime_error("Value is null or empty");
            }

            if constexpr(std::is_same_v<T, std::string>) {
                return str_val;
            } else if constexpr(std::is_same_v<T, bool>) {
                std::transform(str_val.begin(), str_val.end(), str_val.begin(), ::tolower);
                if(str_val == "true" || str_val == "yes" || str_val == "1") return true;
                if(str_val == "false" || str_val == "no" || str_val == "0") return false;
                throw std::runtime_error("Invalid boolean value: '" + str_val + "'");
            } else if constexpr(std::is_integral_v<T>) {
                // First try parsing as double to handle scientific notation
                size_t pos;
                const auto double_val = std::stod(str_val, &pos);
                if(pos != str_val.length()) {
                    throw std::runtime_error("Invalid number format for value: '" + str_val + "'");
                }
                // Check if the double value is actually an integer
                if(double_val != std::floor(double_val)) {
                    throw std::runtime_error("Value must be an integer, got: '" + str_val + "'");
                }
                return static_cast<T>(double_val);
            } else if constexpr(std::is_floating_point_v<T>) {
                size_t pos;
                const auto value = std::stod(str_val, &pos);
                if(pos != str_val.length()) {
                    throw std::runtime_error("Invalid floating point format for value: '" + str_val + "'");
                }
                return static_cast<T>(value);
            }

            throw std::runtime_error("Unsupported type conversion for value: '" + str_val + "'");
        } catch(const std::exception& e) {
            const auto key =
                node_.has_key() ? std::string(std::string_view(node_.key().data(), node_.key().size())) : "<no key>";
            throw std::runtime_error(std::string("Value conversion failed for node '") + key + "': " + e.what());
        }
    }

    /**
     * @brief Retrieve a child node's scalar value by key with strict validation.
     * @tparam T The type to convert to (e.g., int, double, std::string, bool)
     * @param key The key to look up.
     * @return The child node's value converted to type T.
     * @throws std::runtime_error if:
     *         - The current node is invalid
     *         - The current node is not a map
     *         - The key doesn't exist
     *         - The value cannot be converted to type T
     * @example
     *   // Throws if 'port' doesn't exist or cannot be converted to int
     *   int port = config.get<int>("port");
     */
    template<typename T>
    [[nodiscard]] T get(const std::string& key) const {
        return child(key).as<T>();
    }

    /**
     * @brief Retrieve a child node's scalar value by key with a fallback default.
     * @tparam T The type to convert to (e.g., int, double, std::string, bool)
     * @param key The key to look up.
     * @param default_value Value to return if the key doesn't exist or value cannot be converted.
     * @return The child node's value converted to type T, or default_value if:
     *         - The current node is invalid
     *         - The current node is not a map
     *         - The key doesn't exist
     *         - The value cannot be converted to type T
     * @example
     *   // Returns 8080 if 'port' doesn't exist or cannot be converted to int
     *   int port = config.get<int>("port", 8080);
     */
    template<typename T>
    [[nodiscard]] T get(const std::string& key, const T& default_value) const {
        if(!is_valid() || !is_map() || !node_.has_child(ryml::to_csubstr(key))) {
            return default_value;
        }

        auto child_node = node_[ryml::to_csubstr(key)];
        if(!child_node.has_val() || child_node.val().empty()) {
            return default_value;
        }

        try {
            Configuration child_config(tree_, child_node, buffer_);
            return child_config.as<T>();
        } catch(...) {
            return default_value;
        }
    }

    /**
     * @brief Set the value of a key in a map node.
     * @param key The key to set or update.
     * @param value The value to associate with the key.
     * @throws std::runtime_error if the current node is invalid or not a map.
     * @note If the key already exists, its value will be updated. If the key does not exist, it will be added.
     * @example
     *   // For YAML:
     *   // settings:
     *   //   port: 8080
     *   //   host: localhost
     *   config.child("settings").set("port", "9090");  // Updates port to 9090
     *   config.child("settings").set("new_key", "value");  // Adds new_key with value "value"
     */
    void set(const std::string& key, const std::string& value) {
        if(!is_valid()) {
            throw std::runtime_error("Cannot set key '" + key + "' on invalid node");
        }
        if(!node_.is_map()) {
            throw std::runtime_error("Cannot set key '" + key + "' on non-map node");
        }

        auto key_csub = ryml::to_csubstr(key);
        auto val_csub = ryml::to_csubstr(value);

        node_[key_csub] << val_csub;
    }

    /**
     * @brief Iterate over each child node of a map or sequence.
     * @tparam Func A callable type with signature void(const Configuration&)
     * @param func Callback function to be invoked for each child node
     * @note
     *   - For maps: iterates over all values (not keys)
     *   - For sequences: iterates over all elements
     *   - For scalar nodes: no iteration (returns immediately)
     *   - For invalid nodes: no iteration (returns immediately)
     * @example
     *   // For YAML map:
     *   // settings:
     *   //   port: 8080
     *   //   host: localhost
     *   config.child("settings").for_each_child([](const Configuration& child) {
     *     std::cout << child.as<std::string>() << "\n";
     *   });
     *
     *   // For YAML sequence:
     *   // items:
     *   //   - item1
     *   //   - item2
     *   config.child("items").for_each_child([](const Configuration& child) {
     *     std::cout << child.as<std::string>() << "\n";
     *   });
     */
    template<typename Func>
    void for_each_child(Func func) const {
        // Early return for invalid nodes
        if(!is_valid()) {
            return;
        }

        // Early return for scalar nodes
        if(!is_map() && !is_seq()) {
            return;
        }

        // Iterate over children
        for(const ryml::NodeRef ch : node_.children()) {
            func(Configuration(tree_, ch, buffer_));
        }
    }

    /**
     * @brief Convert the entire YAML document to a formatted string.
     * @note This method always dumps the entire YAML document, not just the current node.
     * @return A string representation of the complete YAML document.
     * @example
     *   // For YAML:
     *   // settings:
     *   //   port: 8080
     *   //   host: localhost
     *
     *   config.dump();  // Returns entire document
     *   config.child("settings").dump();  // Also returns entire document
     */
    std::string dump() const {
        if(!is_valid()) {
            return "{invalid}";
        }
        return c4::yml::emitrs<std::string>(*tree_);
    }

    /**
     * @brief Convert only the current node and its subtree to a formatted string.
     * @return A string representation of the current node's subtree.
     * @example
     *   // For YAML:
     *   // settings:
     *   //   port: 8080
     *   //   host: localhost
     *
     *   config.dump_node();  // Returns entire document
     *   config.child("settings").dump_node();  // Returns only:
     *   // port: 8080
     *   // host: localhost
     */
    std::string dump_node() const {
        if(!is_valid()) {
            return "{invalid}";
        }
        return c4::yml::emitrs<std::string>(node_);
    }

    /**
     * @brief Convert the entire YAML document to a compact single-line string.
     * @note This method always dumps the entire YAML document, not just the current node.
     */
    std::string dump_compact() const {
        if(!is_valid()) {
            return "{invalid}";
        }

        std::string result = c4::yml::emitrs<std::string>(*tree_);
        return compact_yaml_string(result);
    }

    /**
     * @brief Convert only the current node and its subtree to a compact single-line string.
     */
    std::string dump_node_compact() const {
        if(!is_valid()) {
            return "{invalid}";
        }

        std::string result = c4::yml::emitrs<std::string>(node_);
        return compact_yaml_string(result);
    }

    /**
     * @brief Create a deep copy of the current configuration node.
     * @return A new Configuration object that is an independent deep copy of the current node.
     * @note If the current node is invalid, an invalid Configuration is returned.
     */
    Configuration deep_copy() const {
        if(!is_valid()) {
            return {};
        }
        std::string yaml_subtree = dump_node();
        return Configuration::from_string(yaml_subtree);
    }

    /**
     * @brief Remove all occurrences of the specified key recursively.
     *
     * This method traverses the current YAML subtree (starting at the current node)
     * and, if any map contains a key matching the provided key, removes that key-value pair.
     * If the key is not found anywhere in the subtree, nothing is done.
     *
     * @param key The key to search for and remove.
     *
     * @note The removal is performed only on the descendants of the current node.
     *       The current node itself will not be removed even if its key matches.
     */
    void remove_key(const std::string& key) {
        if(!is_valid()) {
            return;
        }
        remove_key_recursive(node_, key);
    }

private:
    // Helper function to convert YAML string to compact form
    static std::string compact_yaml_string(const std::string& yaml) {
        std::string compact;
        bool space_needed = false;

        for(char c : yaml) {
            if(c == '\n') {
                if(!compact.empty() && compact.back() != ' ') {
                    space_needed = true;
                }
                continue;
            }
            if(c != ' ') {
                if(space_needed) {
                    compact += ' ';
                    space_needed = false;
                }
                compact += c;
            } else if(!compact.empty() && compact.back() != ' ') {
                compact += ' ';
            }
        }

        return compact;
    }

    /**
     * @brief Recursively traverse the given node and remove any map child whose key matches the target.
     *
     * @param node The current node to examine.
     * @param key  The key to remove.
     */
    static void remove_key_recursive(ryml::NodeRef node, const std::string& key) {
        if(node.is_map()) {
            for(size_t i = node.num_children(); i > 0; --i) {
                size_t idx = i - 1;
                ryml::NodeRef child = node.child(idx);
                if(child.has_key() && child.key() == ryml::to_csubstr(key)) {
                    node.remove_child(idx);
                    continue;
                }
                remove_key_recursive(child, key);
            }
        } else if(node.is_seq()) {
            for(size_t i = node.num_children(); i > 0; --i) {
                ryml::NodeRef child = node.child(i - 1);
                remove_key_recursive(child, key);
            }
        }
    }

    /**
     * @brief Internal constructor to wrap an existing ryml::Tree and ryml::NodeRef,
     * taking ownership of the buffer.
     */
    Configuration(std::shared_ptr<ryml::Tree> tree, ryml::NodeRef node, std::shared_ptr<std::vector<char>> buffer)
        : tree_(std::move(tree)), node_(node), buffer_(std::move(buffer)) {}

    /**
     * @brief Internal constructor that does NOT take ownership of the buffer.
     * This is mainly used when creating a child Configuration that still shares
     * the same underlying buffer.
     */
    Configuration(std::shared_ptr<ryml::Tree> tree, ryml::NodeRef node, const std::vector<char>& /*buffer*/)
        : tree_(std::move(tree)), node_(node) // Does not copy or manage the existing buffer
    {}

    // A shared pointer to the RyML tree, ensuring it won't be destroyed while in use.
    std::shared_ptr<ryml::Tree> tree_;

    // Reference to the current YAML node.
    ryml::NodeRef node_;

    /**
     * @brief The parse buffer for parse_in_place, owned by this Configuration.
     * As long as at least one Configuration keeps it alive, the data remains valid.
     */
    std::shared_ptr<std::vector<char>> buffer_;
};
