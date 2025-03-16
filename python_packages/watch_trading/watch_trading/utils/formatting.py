from datetime import datetime
class FormatString:
    @staticmethod
    def from_bool(value: bool) -> str:
        return "✅" if value else "❌"

    @staticmethod
    def from_timestamp_yyyymmdd_hhmmss_micros(timestamp: str) -> str:
        try:
            dt = datetime.strptime(timestamp, "%Y%m%d_%H%M%S_%f")
            return dt.strftime("%Y-%m-%d %H:%M:%S.%f")
        except ValueError:
            return "Invalid timestamp"
