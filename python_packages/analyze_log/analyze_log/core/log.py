# analyze_log/analyze_log/core/log.py
from pathlib import Path
from typing import List, Optional
from dataclasses import dataclass

@dataclass
class LogEntry:
    """Base class for log entries"""
    timestamp: str
    component: str
    level: str
    content: str

    @classmethod
    def from_line(cls, line: str) -> Optional['LogEntry']:
        """Parse a log line into a LogEntry object"""
        line = line.rstrip('\n')
        parts = line.split(' | ', 3)

        if len(parts) != 4:
            return None

        return cls(
            timestamp=parts[0],
            component=parts[1],
            level=parts[2],
            content=parts[3]
        )

class LogReader:
    """Common log reader for all analyzers"""
    @staticmethod
    def read_log(file_path: Path) -> List[LogEntry]:
        entries = []
        with open(file_path, 'r') as f:
            for line in f:
                entry = LogEntry.from_line(line)
                if entry:
                    entries.append(entry)
        return entries

