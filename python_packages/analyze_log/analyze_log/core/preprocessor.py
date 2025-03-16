# analyze_log/analyze_log/core/preprocessor.py
from typing import List
from .log import LogEntry

class LogPreprocessor:
    """Clean up trading log entries based on specific rules"""

    @staticmethod
    def remove_pre_trading_entries(entries: List[LogEntry]) -> List[LogEntry]:
        """
        Remove all entries before trading starts.

        Args:
            entries: List of LogEntry objects

        Returns:
            Cleaned list of LogEntry objects

        Raises:
            ValueError: If trading start line is not found
        """
        start_idx = -1
        for i, entry in enumerate(entries):
            if ('action=start_trading' in entry.content and
                'result=pass' in entry.content and
                'trading started successfully' in entry.content):
                start_idx = i
                break

        if start_idx == -1:
            raise ValueError(
                "Error: Could not find trading start line. "
                "Log must contain a line with 'action=start_trading' "
                "and 'trading started successfully'"
            )

        return entries[start_idx + 1:]
