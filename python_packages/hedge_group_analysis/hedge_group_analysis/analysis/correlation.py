from typing import Dict, List
from scipy.stats import pearsonr


class CorrelationAnalysis:
    """Analyzes correlation between hedge latency and PnL losses"""

    def __init__(self):
        self.hedge_groups = {}  # Maps hedge group ID to its data
        self.latencies = {}     # Maps hedge group ID to its latency
        self.events_by_group = {}

    def load_duration_events(self, hedge_group_id: str, events: List):
        """
        Load events from DurationAnalysis for a specific hedge group

        Args:
            hedge_group_id: The ID of the hedge group
            events: List of Event objects from DurationAnalysis
        """
        self.events_by_group[hedge_group_id] = events

    def load_pnl_data(self, hedge_group_id: str, pnl_data: Dict):
        """
        Load PnL data from SummaryAnalysis for a specific hedge group

        Args:
            hedge_group_id: The ID of the hedge group
            pnl_data: Dictionary containing PnL data
        """
        self.hedge_groups[hedge_group_id] = pnl_data

    def calculate_hedge_latency(self):
        """
        Calculate hedge latency for each hedge group

        Hedge latency is defined as:
        time of hedge_exchange_fill - time of quote_strategy_notified_time
        """
        for hedge_group_id, events in self.events_by_group.items():
            # Find all quotes with strategy_notified_time
            quote_notifications = []
            for event in events:
                if event.venue_role == 'quote' and 'strategy_notified_time' in event.name:
                    quote_notifications.append(event)

            # Find all hedge fills
            hedge_fills = []
            for event in events:
                if event.venue_role == 'hedge' and 'exchange_fill_time' in event.name:
                    hedge_fills.append(event)

            # If we have both quote notifications and hedge fills
            if quote_notifications and hedge_fills:
                # Verify consecutive pattern as described
                found_valid_pattern = False

                for i in range(len(events) - 3):
                    if (events[i].venue_role == 'quote' and 'strategy_notified_time' in events[i].name and
                        events[i+1].venue_role == 'hedge' and 'send_time_oms' in events[i+1].name and
                        events[i+2].venue_role == 'hedge' and 'live_time_exchange' in events[i+2].name and
                        events[i+3].venue_role == 'hedge' and 'exchange_fill_time' in events[i+3].name):

                        # Calculate latency between quote notification and hedge fill
                        latency = events[i+3].timestamp - events[i].timestamp
                        self.latencies[hedge_group_id] = latency.total_seconds()
                        found_valid_pattern = True
                        break

                if not found_valid_pattern:
                    # Try the fallback: just use first quote notification and first hedge fill
                    quote_time = quote_notifications[0].timestamp
                    hedge_time = hedge_fills[0].timestamp
                    latency = hedge_time - quote_time
                    self.latencies[hedge_group_id] = latency.total_seconds()
            else:
                raise ValueError(f"Hedge group {hedge_group_id} is missing quote notifications or hedge fills")

    def calculate_correlation(self, only_losses=False):
        """
        Calculate correlation between hedge latency and PnL loss

        Args:
            only_losses: If True, only include losing trades in the analysis
        """
        if not self.latencies or not self.hedge_groups:
            raise ValueError("Latency or PnL data missing. Run calculate_hedge_latency first.")

        # Prepare data for correlation
        latencies = []
        pnls = []

        for hg_id, latency in self.latencies.items():
            if hg_id in self.hedge_groups:
                # Skip winning trades if only_losses is True
                if only_losses and self.hedge_groups[hg_id]["win"]["is_win"]:
                    continue

                latencies.append(latency)
                pnls.append(self.hedge_groups[hg_id]["pnl_with_fee"])

        if len(latencies) < 2:
            return {
                "correlation": None,
                "p_value": None,
                "message": "Not enough data points for correlation analysis"
            }

        # Calculate Pearson correlation
        correlation, p_value = pearsonr(latencies, pnls)

        return {
            "correlation": correlation,
            "p_value": p_value,
            "data_points": len(latencies)
        }

    def report(self, only_losses=False):
        """
        Generate report on hedge latency and its correlation with PnL

        Args:
            only_losses: If True, only include losing trades in the analysis
        """
        # Calculate latency if not already done
        if not self.latencies:
            self.calculate_hedge_latency()

        # Calculate correlation
        correlation_results = self.calculate_correlation(only_losses)

        # Determine what type of analysis we're reporting
        analysis_type = "Loss Trades Only" if only_losses else "All Trades"

        # Print correlation results only
        print(f"\nCorrelation Analysis ({analysis_type}):")

        # Count total samples and loss samples
        total_samples = len(self.latencies)
        loss_samples = sum(1 for hg_id in self.latencies if
                          hg_id in self.hedge_groups and
                          not self.hedge_groups[hg_id]["win"]["is_win"])

        if only_losses:
            print(f"Number of samples: {loss_samples} (out of {total_samples} total trades)")
        else:
            print(f"Number of samples: {total_samples}")
        if correlation_results.get("correlation") is not None:
            print(f"Pearson correlation coefficient: {correlation_results['correlation']:.4f}")
            print(f"P-value: {correlation_results['p_value']:.4f}")
            print(f"Number of data points: {correlation_results['data_points']}")

            # Interpret correlation
            if abs(correlation_results['correlation']) < 0.3:
                strength = "weak"
            elif abs(correlation_results['correlation']) < 0.7:
                strength = "moderate"
            else:
                strength = "strong"

            direction = "positive" if correlation_results['correlation'] > 0 else "negative"

            significance = "statistically significant" if correlation_results['p_value'] < 0.05 else "not statistically significant"

            print(f"\nInterpretation: There is a {strength} {direction} correlation between hedge latency and PnL ({significance}).")

            if direction == "negative" and significance == "statistically significant":
                print("This suggests that longer hedge latencies are associated with lower PnL (higher losses).")
            elif direction == "positive" and significance == "statistically significant":
                print("This suggests that longer hedge latencies are associated with higher PnL (which is counterintuitive).")
        else:
            print(correlation_results.get("message", "Insufficient data for correlation analysis."))
