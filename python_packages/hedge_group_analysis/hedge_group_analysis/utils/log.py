import os
import re
import subprocess
from typing import List


class LogEntry:
    def __init__(self, timestamp: str, component: str, level: str, message: str):
        self.timestamp = timestamp
        self.component = component
        self.level = level
        self.message = message


class LogEntryFilter:
    def __init__(self, log_file_path: str):
        if not os.path.exists(log_file_path):
            raise FileNotFoundError(f"Log file not found at {log_file_path}")

        self.log_file_path = log_file_path
        self.line_pattern = re.compile(
            r'^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+) \| ([A-Z]+) \| ([A-Z]+) \| (.+)$'
        )

    def get_matching_entries(self, search_term: str) -> List[LogEntry]:
        try:
            grep_process = subprocess.Popen(
                ['grep', '-a', search_term, self.log_file_path],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )

            grep_output, error = grep_process.communicate()

            if error:
                print(f"Error running grep: {error}")
                return []

            filtered_entries = []
            for line in grep_output.splitlines():
                filtered_entries.append(self._parse_log_line(line))

            return filtered_entries
        except Exception as error:
            print(f"Error processing log file: {str(error)}")
            return []

    def _parse_log_line(self, line: str) -> LogEntry:
        match = self.line_pattern.match(line)
        if match is None:
            raise ValueError(f"Could not parse log line: {line}")
        timestamp, component, level, message = match.groups()
        return LogEntry(
            timestamp=timestamp,
            component=component,
            level=level,
            message=message
        )
