# analyze_log/analyze_log/analyzers/failure/analyzer.py
from typing import Dict, Tuple
from analyze_log.core import Analyzer
from .models import FailureStats

class FailureAnalyzer(Analyzer):
    def analyze(self, stats: FailureStats) -> Dict[str, Dict[str, Tuple[int, int]]]:
        """Analyze failure statistics

        Returns:
            Dict[str, Dict[str, Tuple[int, int]]]: A dictionary where:
                - outer key: action name
                - inner key: reason
                - value: tuple of (count, percentage)
        """
        result = {}

        for action, reasons in stats.action_failures.items():
            total = sum(reasons.values())
            result[action] = {
                reason: (count, int(count / total * 100))
                for reason, count in reasons.items()
            }

        return result
