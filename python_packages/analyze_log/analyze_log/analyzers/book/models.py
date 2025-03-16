# analyze_log/analyze_log/analyzers/book/models.py
from dataclasses import dataclass, field
from typing import Dict, List
from statistics import mean, median

@dataclass
class ExchangeBookStats:
    """Statistics for one exchange's book delays"""
    delays: List[float] = field(default_factory=list)

    @property
    def avg_delay(self) -> float:
        """Calculate average delay"""
        return mean(self.delays) if self.delays else 0.0

    @property
    def median_delay(self) -> float:
        """Calculate median delay"""
        return median(self.delays) if self.delays else 0.0

    @property
    def min_delay(self) -> float:
        """Get minimum delay"""
        return min(self.delays) if self.delays else 0.0

    @property
    def max_delay(self) -> float:
        """Get maximum delay"""
        return max(self.delays) if self.delays else 0.0

@dataclass
class BookStats:
    """Container for all exchanges' book statistics"""
    exchange_stats: Dict[str, ExchangeBookStats] = field(
        default_factory=lambda: {}
    )

    def add_delay(self, exchange: str, delay: float) -> None:
        """Add a delay measurement for an exchange"""
        if exchange not in self.exchange_stats:
            self.exchange_stats[exchange] = ExchangeBookStats()
        self.exchange_stats[exchange].delays.append(delay)
