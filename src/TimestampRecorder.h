#include <chrono>
#include <cstddef>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

class TimestampRecorder {
public:
    enum class Event {
        StartStrategy,
        StartTrading,
        StopTrading,
        OrderPlaced,
        OrderFilled,
        OrderCancelled
        // Add more events as needed
    };

    struct EventHash {
        size_t operator()(const Event& event) const { return static_cast<size_t>(event); }
    };

    // Record timestamp for an event
    void record(Event event) {
        auto now = std::chrono::system_clock::now();
        timestamps_[event] = now;
    }

    // Get timestamp for a specific event
    std::optional<std::chrono::system_clock::time_point> get_timestamp(Event event) const {
        auto timestamp_iter = timestamps_.find(event);
        if(timestamp_iter != timestamps_.end()) {
            return timestamp_iter->second;
        }
        return std::nullopt;
    }

    // Get formatted timestamp string for an event
    std::optional<std::string> get_formatted_timestamp(Event event) const {
        auto timestamp_iter = timestamps_.find(event);
        if(timestamp_iter != timestamps_.end()) {
            auto timepoint = timestamp_iter->second;
            auto timestamp = std::chrono::system_clock::to_time_t(timepoint);

            // Get microseconds
            auto microseconds =
                std::chrono::duration_cast<std::chrono::microseconds>(timepoint.time_since_epoch()).count() % 1000000;

            std::stringstream string_stream;
            string_stream << std::put_time(std::localtime(&timestamp), "%Y-%m-%d %H:%M:%S");
            string_stream << "." << std::setfill('0') << std::setw(6) << microseconds;
            return string_stream.str();
        }
        return std::nullopt;
    }

    // Check if an event has been recorded
    bool has_record(Event event) const { return timestamps_.find(event) != timestamps_.end(); }

    // Clear all recorded timestamps
    void clear() { timestamps_.clear(); }

private:
    std::unordered_map<Event, std::chrono::system_clock::time_point, EventHash> timestamps_;
};
