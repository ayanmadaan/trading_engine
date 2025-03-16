from abc import ABC, abstractmethod
from typing import Any
from rich.console import Console

class Reporter(ABC):
    """Base class for generating reports"""
    def __init__(self):
        self.console = Console()

    @abstractmethod
    def generate_report(self, analysis_result: any) -> None:
        """Generate and display the report"""
        pass
