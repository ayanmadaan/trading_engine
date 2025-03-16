from abc import ABC, abstractmethod
from .log import LogEntry

class DataCollector(ABC):
    """Base class for collecting statistics from log entries"""
    @abstractmethod
    def process_entry(self, entry: LogEntry) -> None:
        """Process a single log entry"""
        pass

    @abstractmethod
    def is_interested(self, entry: LogEntry) -> bool:
        """Determine if this collector is interested in the given entry"""
        pass
