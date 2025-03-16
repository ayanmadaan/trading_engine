# analyze_log/analyze_log/analyzers/severity/collector.py
from datetime import datetime
from analyze_log.core import DataCollector, LogEntry
from .models import SeverityStats, LogItem

class SeverityDataCollector(DataCollector):
    def __init__(self):
        self.stats = SeverityStats()

    def is_interested(self, entry: LogEntry) -> bool:
        """Check if log entry is an error or warning"""
        return entry.level in ("ERRO", "WARN")

    def process_entry(self, entry: LogEntry) -> None:
        """Process a log entry to collect severity statistics"""
        if not self.is_interested(entry):
            return

        log_item = LogItem(
            timestamp=datetime.fromisoformat(entry.timestamp),
            component=entry.component,
            level=entry.level,
            content=entry.content
        )

        self.stats.add_log(entry.component, log_item)
