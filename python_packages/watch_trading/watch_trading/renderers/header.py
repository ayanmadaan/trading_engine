from datetime import datetime

from rich.panel import Panel
from rich.table import Table

from watch_trading.config.panel_colors import PANEL_COLORS
from watch_trading.renderers import BaseRenderer
from watch_trading.utils import FormatString

COLOR = PANEL_COLORS["header"]


class HeaderRenderer(BaseRenderer):

    @staticmethod
    def render(strat_name: str, data: dict) -> Panel:
        now = datetime.now()
        now_str = now.strftime("%Y-%m-%d %H:%M:%S")
        timestamps = data.get("timestamps", {})

        raw_start_time = timestamps.get("start_trading", None)
        if raw_start_time != None:
            start_time = datetime.strptime(raw_start_time, "%Y-%m-%d %H:%M:%S.%f")
            start_time_str = start_time.strftime("%Y-%m-%d %H:%M:%S")
        else:
            start_time_str = "N/A"

        raw_stop_time = timestamps.get("stop_trading", None)
        if raw_stop_time != None:
            stop_time = datetime.strptime(raw_stop_time, "%Y-%m-%d %H:%M:%S.%f")
            stop_time_str = stop_time.strftime("%Y-%m-%d %H:%M:%S")
        else:
            stop_time_str = "N/A"

        if raw_start_time:
            # Calculate trading period
            if not raw_stop_time:
                end_time = now
            else:
                end_time = stop_time
            delta = end_time - start_time
            hours = delta.seconds // 3600
            minutes = (delta.seconds % 3600) // 60
            seconds = delta.seconds % 60

            # Format as "XhYmZs"
            trading_period_str = ""
            if hours > 0:
                trading_period_str += f"{hours}h"
            if minutes > 0:
                trading_period_str += f"{minutes}m"
            if (
                seconds > 0 or not trading_period_str
            ):  # show seconds if no hours/minutes or if there are seconds
                trading_period_str += f"{seconds}s"
        else:
            trading_period_str = "N/A"

        # Create single table for all information
        table = Table(
            show_header=False,
            box=None,
            collapse_padding=True,
        )
        table.add_column("Label", style=f"bold {COLOR}")
        table.add_column("Value", justify="left")

        # Add timing information
        table.add_row("Monitor Time", now_str)
        table.add_row("Start Trading", start_time_str)
        table.add_row("Trading Period", trading_period_str)

        # Add status information
        table.add_row(
            "Hedging Enabled",
            FormatString.from_bool(data["trading"]["enabled"]["hedging"]),
        )
        table.add_row(
            "Quoting Enabled",
            FormatString.from_bool(data["trading"]["enabled"]["quoting"]),
        )

        return Panel(
            table,
            title=f"[bold {COLOR}]Trading Monitor - {strat_name}[/bold {COLOR}]",
            border_style=COLOR,
            expand=True,
            highlight=True,
            safe_box=True,
        )
