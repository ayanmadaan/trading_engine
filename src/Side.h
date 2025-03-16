#pragma once

#include <algorithm>
#include <string>

class Side {
public:
    enum class Type { Ask, Bid };

    // Constructor
    constexpr explicit Side(Type side_type)
        : side_type_(side_type) {}

    // Static factory methods
    [[nodiscard]]
    static constexpr Side ask() {
        return Side(Type::Ask);
    }
    [[nodiscard]]
    static constexpr Side bid() {
        return Side(Type::Bid);
    }

    // Basic type operations
    [[nodiscard]]
    constexpr Type get_type() const {
        return side_type_;
    }
    [[nodiscard]]
    constexpr operator Type() const {
        return side_type_;
    }

    // Sign operation
    static constexpr int sign(Type type) { return type == Type::Ask ? 1 : -1; }

    // Comparison operations based on side
    [[nodiscard]]
    constexpr bool is_inner_or_equal(double price_check, double price_reference) const {
        return side_type_ == Type::Ask ? price_check <= price_reference : price_check >= price_reference;
    }

    [[nodiscard]]
    constexpr bool is_inner(double price_check, double price_reference) const {
        return side_type_ == Type::Ask ? price_check < price_reference : price_check > price_reference;
    }

    [[nodiscard]]
    constexpr bool is_away_or_equal(double price_check, double price_reference) const {
        return side_type_ == Type::Ask ? price_check >= price_reference : price_check <= price_reference;
    }

    [[nodiscard]]
    constexpr bool is_away(double price_check, double price_reference) const {
        return side_type_ == Type::Ask ? price_check > price_reference : price_check < price_reference;
    }

    // Price adjustment operations
    [[nodiscard]]
    constexpr double add_away(double base_price, double price_offset) const {
        return side_type_ == Type::Ask ? base_price + price_offset : base_price - price_offset;
    }

    [[nodiscard]]
    constexpr double add_inner(double base_price, double price_offset) const {
        return side_type_ == Type::Ask ? base_price - price_offset : base_price + price_offset;
    }

    [[nodiscard]]
    constexpr double get_inner(double price_first, double price_second) const {
        return side_type_ == Type::Ask ? std::min(price_first, price_second) : std::max(price_first, price_second);
    }

    [[nodiscard]]
    constexpr double get_away(double price_first, double price_second) const {
        return side_type_ == Type::Ask ? std::max(price_first, price_second) : std::min(price_first, price_second);
    }

    [[nodiscard]]
    constexpr std::string to_string() const {
        return side_type_ == Type::Ask ? "ask" : "bid";
    }

    // Other side operations
    class other {
    public:
        static constexpr Type type(Type original_side) { return original_side == Type::Ask ? Type::Bid : Type::Ask; }

        static constexpr Side side(const Side& original_side) { return Side(type(original_side.get_type())); }

        static constexpr double add_away(const Side& original_side, double base_price, double price_offset) {
            return side(original_side).add_away(base_price, price_offset);
        }

        static constexpr double add_inner(const Side& original_side, double base_price, double price_offset) {
            return side(original_side).add_inner(base_price, price_offset);
        }

        [[nodiscard]]
        static constexpr double get_inner(const Side& original_side, double price_first, double price_second) {
            return side(original_side).get_inner(price_first, price_second);
        }

        [[nodiscard]]
        static constexpr double get_away(const Side& original_side, double price_first, double price_second) {
            return side(original_side).get_away(price_first, price_second);
        }
    };

    // Comparison operators
    [[nodiscard]]
    constexpr bool operator==(const Side& rhs) const {
        return side_type_ == rhs.side_type_;
    }
    [[nodiscard]]
    constexpr bool operator!=(const Side& rhs) const {
        return side_type_ != rhs.side_type_;
    }

private:
    Type side_type_;
};

// Helper constants
constexpr Side ASK{Side::Type::Ask};
constexpr Side BID{Side::Type::Bid};
