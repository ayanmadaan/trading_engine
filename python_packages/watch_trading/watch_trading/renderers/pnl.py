from dataclasses import dataclass
from typing import Any

from rich.panel import Panel
from rich.table import Table

from watch_trading.config.panel_colors import PANEL_COLORS
from watch_trading.renderers import BaseRenderer

COLOR = PANEL_COLORS["pnl"]

JSONData = dict[str, Any]


@dataclass
class FeesData:
    maker: float
    taker: float

    @staticmethod
    def from_dict(data: JSONData) -> "FeesData":
        try:
            return FeesData(
                maker=float(data["maker"]),
                taker=float(data["taker"])
            )
        except ValueError as e:
            # Check which conversion failed
            for key in ["maker", "taker"]:
                try:
                    float(data[key])
                except (ValueError, TypeError):
                    raise ValueError(f"Failed to convert '{key}' value '{data[key]}' (type: {type(data[key]).__name__}) to float") from e
            raise e
        except KeyError as e:
            raise KeyError(f"Missing required key: {e}") from e


@dataclass
class RealizedData:
    with_fee: float
    without_fee: float

    @staticmethod
    def from_dict(data: JSONData) -> "RealizedData":
        try:
            return RealizedData(
                with_fee=float(data["with_fee"]),
                without_fee=float(data["without_fee"])
            )
        except ValueError as e:
            # Check which conversion failed
            for key in ["with_fee", "without_fee"]:
                try:
                    float(data[key])
                except (ValueError, TypeError):
                    raise ValueError(f"Failed to convert '{key}' value '{data[key]}' (type: {type(data[key]).__name__}) to float") from e
            raise e
        except KeyError as e:
            raise KeyError(f"Missing required key: {e}") from e


@dataclass
class PnLData:
    average_cost: float
    fees: FeesData
    realized: RealizedData
    total: RealizedData
    unrealized: float

    @staticmethod
    def from_dict(data: JSONData) -> "PnLData":
        try:
            # Handle float conversions first
            try:
                average_cost = float(data["average_cost"])
            except (ValueError, TypeError) as e:
                raise ValueError(f"Failed to convert 'average_cost' value '{data['average_cost']}' (type: {type(data['average_cost']).__name__}) to float") from e

            try:
                unrealized = float(data["unrealized"])
            except (ValueError, TypeError) as e:
                raise ValueError(f"Failed to convert 'unrealized' value '{data['unrealized']}' (type: {type(data['unrealized']).__name__}) to float") from e

            # Now handle the nested objects
            try:
                fees = FeesData.from_dict(data["fees"])
            except KeyError:
                raise KeyError("Missing required key: 'fees'")
            except Exception as e:
                raise ValueError(f"Error in 'fees' data: {str(e)}") from e

            try:
                realized = RealizedData.from_dict(data["realized"])
            except KeyError:
                raise KeyError("Missing required key: 'realized'")
            except Exception as e:
                raise ValueError(f"Error in 'realized' data: {str(e)}") from e

            try:
                total = RealizedData.from_dict(data["total"])
            except KeyError:
                raise KeyError("Missing required key: 'total'")
            except Exception as e:
                raise ValueError(f"Error in 'total' data: {str(e)}") from e

            return PnLData(
                average_cost=average_cost,
                fees=fees,
                realized=realized,
                total=total,
                unrealized=unrealized,
            )
        except KeyError as e:
            raise KeyError(f"Missing required key: {e}") from e


class PnLRenderer(BaseRenderer):

    @classmethod
    def render(cls, data: JSONData) -> Panel:
        pnl_data = PnLData.from_dict(data)

        table = Table(
            show_header=False,
            box=None,
            collapse_padding=True,
        )

        # Add columns for label and value
        table.add_column("Label", style=f"bold {COLOR}")
        table.add_column("Value", justify="right")

        # Add rows
        table.add_row("Average Cost", f"{pnl_data.average_cost:.6f}")
        table.add_row("Maker Fees", f"{pnl_data.fees.maker:.6f}")
        table.add_row("Taker Fees", f"{pnl_data.fees.taker:.6f}")
        table.add_row("Realized PnL With Fees", f"{pnl_data.realized.with_fee:.6f}")
        table.add_row(
            "Realized PnL Without Fees", f"{pnl_data.realized.without_fee:.6f}"
        )
        table.add_row("Unrealized PnL", f"{pnl_data.unrealized:.6f}")
        table.add_row("Total PnL With Fees", f"{pnl_data.total.with_fee:.6f}")
        table.add_row("Total PnL Without Fees", f"{pnl_data.total.without_fee:.6f}")

        return Panel(
            table,
            title=f"[bold {COLOR}]PnL Summary[/bold {COLOR}]",
            border_style=COLOR,
            expand=True,
            highlight=True,
            safe_box=True,
        )
