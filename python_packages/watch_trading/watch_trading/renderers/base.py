from abc import ABC, abstractmethod
from typing import Any
from rich.table import Table
from rich.panel import Panel

class BaseRenderer(ABC):

    @abstractmethod
    def render(self, data:dict[str, Any]) -> Table|Panel:
        pass
