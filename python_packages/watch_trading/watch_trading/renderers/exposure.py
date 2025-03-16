from typing import Any

from rich.panel import Panel
from rich.table import Table

from watch_trading.config.panel_colors import PANEL_COLORS
from watch_trading.renderers import BaseRenderer
from watch_trading.utils import FormatString

COLOR = PANEL_COLORS["exposure"]


class ExposureRenderer(BaseRenderer):

    @staticmethod
    def render(exposure: dict[str, Any]) -> Panel:
        table = Table(show_header=False, box=None, collapse_padding=True)

        # Add columns for label and value
        table.add_column("Label", style=f"bold {COLOR}")
        table.add_column("Value", justify="right")

        # Add rows
        table.add_row("Net Zero", FormatString.from_bool(exposure["is_net_zero"]))
        table.add_row("Net Exposure", f"{exposure['net_exposure']:.6f}")

        return Panel(
            table,
            title=f"[bold {COLOR}]Exposure",
            border_style=COLOR,
            expand=True,
            highlight=True,
            safe_box=True,
        )
