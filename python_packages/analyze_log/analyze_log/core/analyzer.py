from abc import ABC, abstractmethod
from typing import Any

class Analyzer(ABC):
    """Base class for analyzing collected data"""
    @abstractmethod
    def analyze(self, data: any) -> any:
        """Analyze the collected data"""
        pass
