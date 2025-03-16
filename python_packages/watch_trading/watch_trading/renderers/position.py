from dataclasses import dataclass
from typing import Any

from rich.panel import Panel
from rich.table import Table

from watch_trading.config.panel_colors import PANEL_COLORS
from watch_trading.renderers import BaseRenderer

COLOR = PANEL_COLORS["position"]
JSONData = dict[str, Any]


@dataclass
class PositionData:
    hedge: float
    max_position: float
    position_ratio: float
    quote: float

    @staticmethod
    def from_dict(data: JSONData) -> "PositionData":
        try:
            return PositionData(
                hedge=float(data["hedge"]),
                max_position=float(data["max_position"]),
                position_ratio=float(data["position_ratio"]),
                quote=float(data["quote"]),
            )
        except ValueError as e:
            # Find which key caused the conversion error
            for key in ["hedge", "max_position", "position_ratio", "quote"]:
                try:
                    float(data[key])
                except (ValueError, TypeError):
                    raise ValueError(f"Failed to convert '{key}' value '{data[key]}' (type: {type(data[key]).__name__}) to float") from e
            # If we get here, it's likely some other conversion error
            raise e
        except KeyError as e:
            # Handle missing keys
            raise KeyError(f"Missing required key: {e}") from e


class PositionRenderer(BaseRenderer):

    @classmethod
    def render(cls, data: JSONData) -> Panel:
        position_data = PositionData.from_dict(data)

        table = Table(show_header=False, box=None, collapse_padding=True)

        # Add columns for label and value
        table.add_column("Label", style=f"bold {COLOR}")
        table.add_column("Value", justify="right")

        # Add rows
        table.add_row("Quote Position", f"{position_data.quote:.3f}".rjust(8))
        table.add_row("Hedge Position", f"{position_data.hedge:.3f}".rjust(8))
        table.add_row("Max Position", f"{position_data.max_position:.3f}".rjust(8))
        table.add_row("Position Ratio", f"{position_data.position_ratio:.1%}".rjust(7))

        return Panel(
            table,
            title=f"[bold {COLOR}]Position Summary",
            border_style=COLOR,
            expand=True,
            highlight=True,
            safe_box=True,
        )
