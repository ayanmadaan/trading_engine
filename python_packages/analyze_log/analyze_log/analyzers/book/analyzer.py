# analyze_log/analyze_log/analyzers/book/analyzer.py
from typing import Dict, Any
from analyze_log.core import Analyzer
from .models import BookStats

class BookAnalyzer(Analyzer):
    def analyze(self, stats: BookStats) -> Dict[str, Dict[str, Any]]:
        """Analyze book delay statistics for all exchanges"""
        result = {}

        for exchange, exchange_stats in stats.exchange_stats.items():
            result[exchange] = {
                'count': len(exchange_stats.delays),
                'avg_delay': exchange_stats.avg_delay,
                'median_delay': exchange_stats.median_delay,
                'min_delay': exchange_stats.min_delay,
                'max_delay': exchange_stats.max_delay
            }

        return result
