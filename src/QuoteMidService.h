#pragma once

template<typename PositionProvider>
class QuoteMidService {
public:
    struct Config {
        bool use_const_shift{false};
        double const_shift_ratio{0.0};

        bool use_position_shift{false};
        double shift_ratio_per_position{0.0};
    };

    explicit QuoteMidService(Config config, const PositionProvider& provider)
        : config_{std::move(config)}, provider_{provider} {

        validate_config();
    }

    [[nodiscard]]
    double shift(double reference_price) const {
        double total_shift_ratio = 0.0;

        total_shift_ratio += get_const_shift_ratio();
        total_shift_ratio += get_position_shift_ratio();

        return reference_price * (1.0 + total_shift_ratio);
    }

    [[nodiscard]] double get_const_shift_ratio() const {
        if(config_.use_const_shift) {
            return config_.const_shift_ratio;
        } else {
            return 0.0;
        }
    }

    [[nodiscard]] double get_position_shift_ratio() const {
        if(config_.use_position_shift) {
            return -1.0 * provider_.get_position() * config_.shift_ratio_per_position;
        } else {
            return 0.0;
        }
    }

private:
    void validate_config() const {
        if(config_.use_position_shift && config_.shift_ratio_per_position < 0.0) {
            throw std::invalid_argument("position_shift must be non-negative, got: " +
                                        std::to_string(config_.shift_ratio_per_position));
        }
    }

    const Config config_;
    const PositionProvider& provider_;
};
