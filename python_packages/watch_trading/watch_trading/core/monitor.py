from pathlib import Path
import time

from rich.console import Group
from rich.layout import Layout
from rich.live import Live

from watch_trading.renderers import (
    CooldownRenderer,
    ExposureRenderer,
    HeaderRenderer,
    HealthRenderer,
    MarketDataRenderer,
    OrderManagementRenderer,
    PnLRenderer,
    PositionRenderer,
    QuotingReferenceRenderer,
    TradeAnalysisRenderer,
    ProcessAliveMonitor
)

from watch_trading.core import JsonReader

class TradingMonitor:
    def __init__(self, data_path: Path, refresh_interval: float = 1.0):
        self.layout = Layout()
        self.data_path = Path(data_path)
        self.strategy_name = self.data_path.stem
        self.refresh_interval = refresh_interval
        # Add ProcessMonitor instance
        self.process_monitor = ProcessAliveMonitor(
            self.data_path.parent.parent.parent,  # Get project root
            polling_interval=refresh_interval
        )

    def run(self):
        # Create layout structure once
        layout = Layout()
        layout.split_row(
            Layout(name="left", ratio=2),
            Layout(name="right", ratio=1),
        )
        layout["left"].split_column(
            Layout(name="left-top", ratio=1),
            Layout(name="left-bottom", ratio=6),
        )
        layout["left-bottom"].split_row(
            Layout(name="left-bottom-top", ratio=2),
            Layout(name="left-bottom-bottom", ratio=3),
        )
        layout["left-bottom-bottom"].split_column(
            Layout(name="left-bottom-bottom-top", ratio=1),
            Layout(name="left-bottom-bottom-middle", ratio=1),
            Layout(name="left-bottom-bottom-bottom", ratio=3),
        )

        with Live(layout, refresh_per_second=4, screen=True) as live:
            while True:
                data = JsonReader().read(self.data_path)

                # Render panels
                header_panel = HeaderRenderer.render(self.strategy_name, data)
                market_table = MarketDataRenderer.render(data["market_data"])
                exposure_panel = ExposureRenderer.render(data["exposure"])
                pnl_panel = PnLRenderer.render(data["pnl"])
                position_panel = PositionRenderer.render(data["positions"])
                quoting_reference_panel = QuotingReferenceRenderer.render(
                    data["quoting_reference_price"]
                )
                cooldown_panel = CooldownRenderer.render(data["trading"]["cooldowns"])
                health_panel = HealthRenderer.render(data["trading"]["health"])
                order_management_panel = OrderManagementRenderer.render(data)
                trade_analysis_panel = TradeAnalysisRenderer.render(data["trade_analysis"])
                alive_panel = self.process_monitor.render(self.strategy_name)

                # Stack panels
                left_stack = Group(
                    header_panel,
                    exposure_panel,
                    pnl_panel,
                    position_panel,
                    quoting_reference_panel,
                    cooldown_panel,
                )

                # Update layout content
                layout["right"].update(order_management_panel)
                layout["left-top"].update(market_table)
                layout["left-bottom-top"].update(left_stack)
                layout["left-bottom-bottom-top"].update(alive_panel)
                layout["left-bottom-bottom-middle"].update(health_panel)
                layout["left-bottom-bottom-bottom"].update(trade_analysis_panel)

                # Wait before next refresh
                time.sleep(self.refresh_interval)
