from dataclasses import dataclass
from typing import Any

from rich.panel import Panel
from rich.table import Table

from watch_trading.config.panel_colors import PANEL_COLORS
from watch_trading.renderers import BaseRenderer

COLOR = PANEL_COLORS["quoting_reference"]

JSONData = dict[str, Any]


@dataclass
class QuotingReferenceData:
    const_shift_ratio_bps: float
    position_shift_ratio_bps: float
    quote_mid: float
    reference_mid: float

    @staticmethod
    def from_dict(data: JSONData) -> "QuotingReferenceData":
        try:
            return QuotingReferenceData(
                const_shift_ratio_bps=float(data["const_shift_ratio_bps"]),
                position_shift_ratio_bps=float(data["position_shift_ratio_bps"]),
                quote_mid=float(data["quote_mid"]),
                reference_mid=float(data["reference_mid"]),
            )
        except ValueError as e:
            # Find which key caused the conversion error
            for key in ["const_shift_ratio_bps", "position_shift_ratio_bps", "quote_mid", "reference_mid"]:
                try:
                    float(data[key])
                except (ValueError, TypeError):
                    raise ValueError(f"Failed to convert '{key}' value '{data[key]}' (type: {type(data[key]).__name__}) to float") from e
            # If we get here, it's likely some other conversion error
            raise e
        except KeyError as e:
            # Handle missing keys
            raise KeyError(f"Missing required key: {e}") from e


class QuotingReferenceRenderer(BaseRenderer):

    @classmethod
    def render(cls, data: JSONData) -> Panel:
        quoting_data = QuotingReferenceData.from_dict(data)

        table = Table(
            show_header=False, box=None, padding=(0, 1), collapse_padding=True
        )

        # Add columns for label and value
        table.add_column("Label", style=f"bold {COLOR}")
        table.add_column("Value", justify="right")

        # Add rows
        table.add_row("Reference Mid", f"{quoting_data.reference_mid:.1f}")
        table.add_row("Quote Mid", f"{quoting_data.quote_mid:.1f}")
        table.add_row("Const Shift", f"{quoting_data.const_shift_ratio_bps:.2f} bps")
        table.add_row(
            "Position Shift", f"{quoting_data.position_shift_ratio_bps:.2f} bps"
        )

        return Panel(
            table,
            title=f"[bold {COLOR}]Quoting Reference[/bold {COLOR}]",
            border_style=COLOR,
            expand=True,
            highlight=True,
            safe_box=True,
        )
