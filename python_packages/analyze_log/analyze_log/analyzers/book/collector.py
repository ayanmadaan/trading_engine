# analyze_log/analyze_log/analyzers/book/collector.py
import re
from typing import Optional
from analyze_log.core import DataCollector, LogEntry
from .models import BookStats

class BookDataCollector(DataCollector):
    def __init__(self):
        self.stats = BookStats()

    def is_interested(self, entry: LogEntry) -> bool:
        """Check if log entry contains book freshness check"""
        return 'action=check_book_freshness' in entry.content

    def _extract_exchange(self, content: str) -> Optional[str]:
        """Extract exchange name from log content"""
        match = re.search(r'exchange=(\w+)', content)
        return match.group(1) if match else None

    def _extract_delay(self, content: str) -> Optional[float]:
        """Extract delay from log content"""
        match = re.search(r'time_diff_ms=([\d.]+)', content)
        return float(match.group(1)) if match else None

    def process_entry(self, entry: LogEntry) -> None:
        """Process a log entry to collect book delay statistics"""
        if not self.is_interested(entry):
            return

        exchange = self._extract_exchange(entry.content)
        delay = self._extract_delay(entry.content)

        if exchange and delay is not None:
            self.stats.add_delay(exchange, delay)
