#include <cmath>
#include <concepts>

template<typename T>
concept PositionProvider = requires(T provider) {
    { provider.get_position() } -> std::convertible_to<double>;
};

template<PositionProvider QuotePositionProvider, PositionProvider HedgePositionProvider>
class ExposureMonitor {
public:
    ExposureMonitor(double exposure_tolerance, const QuotePositionProvider& quote, const HedgePositionProvider& hedge)
        : exposure_tolerance_(exposure_tolerance), quote_position_manager_(quote), hedge_position_manager_(hedge) {}

    [[nodiscard]] double get_exposure() const {
        return quote_position_manager_.get_position() + hedge_position_manager_.get_position();
    }

    [[nodiscard]] bool has_exposure() const { return std::abs(get_exposure()) > exposure_tolerance_; }

    [[nodiscard]] bool no_exposure() const { return !has_exposure(); }

private:
    double exposure_tolerance_;
    const QuotePositionProvider& quote_position_manager_;
    const HedgePositionProvider& hedge_position_manager_;
};
