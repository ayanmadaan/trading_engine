# analyze_log/analyze_log/analyzers/severity/analyzer.py
from typing import Dict, List, Any
from collections import defaultdict
from analyze_log.core import Analyzer
from .models import SeverityStats, LogItem

class SeverityAnalyzer(Analyzer):
    def analyze(self, stats: SeverityStats) -> Dict[str, Any]:
        """Analyze severity statistics and sort logs"""
        result = {
            'summary': defaultdict(lambda: {'errors': 0, 'warnings': 0}),
            'logs': defaultdict(lambda: {'errors': [], 'warnings': []})
        }

        # Collect statistics and logs
        for component, logs in stats.components.items():
            # Add to summary
            result['summary'][component]['errors'] = len(logs.errors)
            result['summary'][component]['warnings'] = len(logs.warnings)

            # Add sorted logs
            result['logs'][component]['errors'] = sorted(
                logs.errors,
                key=lambda x: x.timestamp
            )
            result['logs'][component]['warnings'] = sorted(
                logs.warnings,
                key=lambda x: x.timestamp
            )

        return result
