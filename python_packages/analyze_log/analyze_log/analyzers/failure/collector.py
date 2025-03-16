# analyze_log/analyze_log/analyzers/failure/collector.py
import re
from typing import Optional
from analyze_log.core import DataCollector, LogEntry
from .models import FailureStats

class FailureDataCollector(DataCollector):
    def __init__(self):
        self.stats = FailureStats()

    def is_interested(self, entry: LogEntry) -> bool:
        """Check if the log entry contains failure information"""
        return 'action=' in entry.content and '=fail' in entry.content

    def _extract_action(self, content: str) -> Optional[str]:
        """Extract action name from log content"""
        match = re.search(r'action=(\w+)', content)
        return match.group(1) if match else None

    def _extract_reason(self, content: str) -> str:
        """Extract failure reason from log content"""
        # Try to match quoted reason first
        quoted_match = re.search(r'reason="([^"]+)"', content)
        if quoted_match:
            return quoted_match.group(1)

        # Try to match unquoted reason
        unquoted_match = re.search(r'reason=(\w+)', content)
        if unquoted_match:
            return unquoted_match.group(1)

        return "no_reason_provided"

    def process_entry(self, entry: LogEntry) -> None:
        """Process a log entry and collect failure statistics"""
        if not self.is_interested(entry):
            return

        action = self._extract_action(entry.content)
        if not action:
            return

        reason = self._extract_reason(entry.content)
        self.stats.add_failure(action, reason)

