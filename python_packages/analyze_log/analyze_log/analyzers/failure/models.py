# analyze_log/analyze_log/analyzers/failure/models.py
from dataclasses import dataclass, field
from collections import defaultdict
from typing import Dict, DefaultDict

@dataclass
class FailureStats:
    """Container for failure statistics"""
    action_failures: DefaultDict[str, DefaultDict[str, int]] = field(
        default_factory=lambda: defaultdict(lambda: defaultdict(int))
    )

    def add_failure(self, action: str, reason: str) -> None:
        """Add a failure record"""
        self.action_failures[action][reason] += 1

    def get_total_failures(self, action: str) -> int:
        """Get total failures for an action"""
        return sum(self.action_failures[action].values())
