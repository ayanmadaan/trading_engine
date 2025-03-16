import numpy as np
from typing import Dict, List, Tuple, Optional
from dataclasses import dataclass
from datetime import datetime, timedelta


@dataclass
class CancelFillData:
    hedge_group_id: str
    order_id: str
    cancel_time: datetime
    fill_time: datetime
    duration_ms: float
    is_win: bool


class CancelFillAnalysis:
    """
    Analyzes the duration between order cancellation requests and fills
    for hedge groups, categorized by win/loss status.
    """

    def __init__(self):
        self.hedge_groups = {}  # Maps hedge group ID to win/loss status
        self.events_by_group = {}
        self.cancel_fill_durations = []  # List of CancelFillData objects

    def load_duration_events(self, hedge_group_id: str, events: List):
        """
        Load events from DurationAnalysis for a specific hedge group

        Args:
            hedge_group_id: The ID of the hedge group
            events: List of Event objects from DurationAnalysis
        """
        self.events_by_group[hedge_group_id] = events

    def load_hedge_group_data(self, hedge_group_id: str, data: Dict):
        """
        Load data from SummaryAnalysis for a specific hedge group

        Args:
            hedge_group_id: The ID of the hedge group
            data: Dictionary containing hedge group data including win/loss status
        """
        self.hedge_groups[hedge_group_id] = data

    def analyze(self):
        """
        Analyze cancel-to-fill durations for all hedge groups with cancellations
        """
        self.cancel_fill_durations = []

        # Process each hedge group
        for hg_id, events in self.events_by_group.items():
            # Skip if we don't have win/loss data
            if hg_id not in self.hedge_groups:
                continue

            is_win = self.hedge_groups[hg_id]["win"]["is_win"]

            # Group events by order ID
            orders = {}
            for event in events:
                if event.order_id not in orders:
                    orders[event.order_id] = []
                orders[event.order_id].append(event)

            # For each order, find cancel and fill times
            for order_id, order_events in orders.items():
                # Find cancel events
                cancel_events = [e for e in order_events if 'cancel_time' in e.name]

                if not cancel_events:
                    continue  # Skip orders without cancellations

                # Find fill events
                fill_events = [e for e in order_events if 'exchange_fill_time' in e.name]

                if not fill_events:
                    continue  # Skip orders without fills

                # Get the first cancel and the first fill after that cancel
                cancel_event = sorted(cancel_events, key=lambda e: e.timestamp)[0]

                # Find fills that occurred after the cancel
                fills_after_cancel = [f for f in fill_events if f.timestamp > cancel_event.timestamp]

                if not fills_after_cancel:
                    continue  # Skip if no fill after cancel

                # Get the first fill after cancel
                fill_event = sorted(fills_after_cancel, key=lambda e: e.timestamp)[0]

                # Calculate duration
                duration = fill_event.timestamp - cancel_event.timestamp
                duration_ms = duration.total_seconds() * 1000

                # Store data
                self.cancel_fill_durations.append(CancelFillData(
                    hedge_group_id=hg_id,
                    order_id=order_id,
                    cancel_time=cancel_event.timestamp,
                    fill_time=fill_event.timestamp,
                    duration_ms=duration_ms,
                    is_win=is_win
                ))

    def get_statistics(self, filter_type: Optional[str] = None):
        """
        Calculate statistics for cancel-to-fill durations

        Args:
            filter_type: Optional filter for trade type ('win', 'loss', or None for all)
        """
        if not self.cancel_fill_durations:
            return {
                "count": 0,
                "message": f"No {'winning' if filter_type == 'win' else 'losing' if filter_type == 'loss' else ''} trades with cancellation-to-fill sequences found."
            }

        # Filter data based on win/loss status if requested
        filtered_data = self.cancel_fill_durations
        if filter_type == 'win':
            filtered_data = [d for d in self.cancel_fill_durations if d.is_win]
        elif filter_type == 'loss':
            filtered_data = [d for d in self.cancel_fill_durations if not d.is_win]

        if not filtered_data:
            return {
                "count": 0,
                "message": f"No {'winning' if filter_type == 'win' else 'losing' if filter_type == 'loss' else ''} trades with cancellation-to-fill sequences found."
            }

        durations = [d.duration_ms for d in filtered_data]

        return {
            "count": len(durations),
            "mean": np.mean(durations),
            "median": np.median(durations),
            "std": np.std(durations),
            "min": np.min(durations),
            "max": np.max(durations),
            "percentile_25": np.percentile(durations, 25),
            "percentile_75": np.percentile(durations, 75),
            "percentile_90": np.percentile(durations, 90),
            "percentile_95": np.percentile(durations, 95),
            "percentile_99": np.percentile(durations, 99)
        }

    def report(self):
        """
        Generate reports on cancel-to-fill durations for winning, losing, and all trades
        """
        # Run analysis if not already done
        if not self.cancel_fill_durations:
            self.analyze()

        # Get statistics for different categories
        all_stats = self.get_statistics()
        win_stats = self.get_statistics('win')
        loss_stats = self.get_statistics('loss')

        # Print header
        print("\nCancel-to-Fill Duration Analysis")
        print("================================")

        # Print summary counts
        total_count = all_stats.get("count", 0)
        win_count = win_stats.get("count", 0)
        loss_count = loss_stats.get("count", 0)

        print(f"Total trades with cancellation-to-fill events: {total_count}")
        print(f"Winning trades with cancellation-to-fill events: {win_count}")
        print(f"Losing trades with cancellation-to-fill events: {loss_count}")

        # Print statistics for each category
        self._print_category_stats("ALL TRADES", all_stats)
        self._print_category_stats("WINNING TRADES", win_stats)
        self._print_category_stats("LOSING TRADES", loss_stats)

        # Print comparative analysis if we have both winning and losing trades
        if win_count > 0 and loss_count > 0:
            print("\nComparative Analysis:")
            win_mean = win_stats.get("mean")
            loss_mean = loss_stats.get("mean")

            if win_mean is not None and loss_mean is not None:
                diff_absolute = loss_mean - win_mean
                diff_percent = ((loss_mean - win_mean) / win_mean * 100) if win_mean > 0 else float('inf')

                print(f"Mean cancel-to-fill duration (winning trades): {win_mean:.2f} ms")
                print(f"Mean cancel-to-fill duration (losing trades): {loss_mean:.2f} ms")
                print(f"Difference: {diff_absolute:.2f} ms ({diff_percent:.2f}%)")

                if loss_mean > win_mean:
                    print("Losing trades have LONGER cancel-to-fill durations than winning trades.")
                else:
                    print("Losing trades have SHORTER cancel-to-fill durations than winning trades.")
            else:
                print("Unable to compare means - statistics unavailable for one or both categories.")

    def _print_category_stats(self, category_name, stats):
        """Helper method to print statistics for a category"""
        print(f"\n{category_name} STATISTICS:")

        if stats.get("count", 0) == 0:
            print(stats.get("message", "No data available."))
            return

        # Print basic statistics
        print(f"Count: {stats['count']}")
        print(f"Mean duration: {stats['mean']:.2f} ms")
        print(f"Median duration: {stats['median']:.2f} ms")
        print(f"Standard deviation: {stats['std']:.2f} ms")
        print(f"Min duration: {stats['min']:.2f} ms")
        print(f"Max duration: {stats['max']:.2f} ms")

        # Print percentiles
        print("Percentiles:")
        print(f"  25th: {stats['percentile_25']:.2f} ms")
        print(f"  75th: {stats['percentile_75']:.2f} ms")
        print(f"  90th: {stats['percentile_90']:.2f} ms")
        print(f"  95th: {stats['percentile_95']:.2f} ms")
        print(f"  99th: {stats['percentile_99']:.2f} ms")
