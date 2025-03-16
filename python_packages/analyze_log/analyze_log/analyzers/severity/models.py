# analyze_log/analyze_log/analyzers/severity/models.py
from dataclasses import dataclass, field
from typing import Dict, List
from datetime import datetime

@dataclass
class LogItem:
    """Single log item with error or warning"""
    timestamp: datetime
    component: str
    level: str
    content: str

@dataclass
class ComponentLogs:
    """Container for logs of one component"""
    errors: List[LogItem] = field(default_factory=list)
    warnings: List[LogItem] = field(default_factory=list)

@dataclass
class SeverityStats:
    """Container for all severity statistics"""
    components: Dict[str, ComponentLogs] = field(default_factory=dict)

    def add_log(self, component: str, log_item: LogItem) -> None:
        if component not in self.components:
            self.components[component] = ComponentLogs()

        if log_item.level == "ERRO":
            self.components[component].errors.append(log_item)
        elif log_item.level == "WARN":
            self.components[component].warnings.append(log_item)
