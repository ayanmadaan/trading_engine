from dataclasses import dataclass
from typing import Any

from rich.panel import Panel
from rich.table import Table

from watch_trading.config.panel_colors import PANEL_COLORS
from watch_trading.renderers import BaseRenderer
from watch_trading.utils import FormatString

COLOR = PANEL_COLORS["health"]

JSONData = dict[str, Any]


@dataclass
class WebsocketStatus:
    status: bool
    reason: str

    @staticmethod
    def from_dict(data: JSONData) -> "WebsocketStatus":
        return WebsocketStatus(status=bool(data["status"]), reason=str(data["reason"]))


@dataclass
class WebsocketsData:
    hedge: WebsocketStatus
    quote: WebsocketStatus

    @staticmethod
    def from_dict(data: JSONData) -> "WebsocketsData":
        return WebsocketsData(
            hedge=WebsocketStatus.from_dict(data["hedge"]),
            quote=WebsocketStatus.from_dict(data["quote"]),
        )


@dataclass
class HealthStatus:
    status: bool
    reason: str

    @staticmethod
    def from_dict(data: JSONData) -> "HealthStatus":
        return HealthStatus(status=bool(data["status"]), reason=str(data["reason"]))


@dataclass
class HealthData:
    net_zero: HealthStatus
    hedger: HealthStatus
    within_max_position: HealthStatus
    within_stop_loss: HealthStatus
    websockets: WebsocketsData

    @staticmethod
    def from_dict(data: JSONData) -> "HealthData":
        return HealthData(
            net_zero=HealthStatus.from_dict(data["net_zero"]),
            hedger=HealthStatus.from_dict(data["hedger"]),
            within_max_position=HealthStatus.from_dict(data["no_max_position"]),
            within_stop_loss=HealthStatus.from_dict(data["no_stop_loss"]),
            websockets=WebsocketsData.from_dict(data["websockets"]),
        )


class HealthRenderer(BaseRenderer):

    @classmethod
    def render(cls, data: JSONData) -> Panel:
        health_data = HealthData.from_dict(data)

        table = Table(show_header=False, box=None, collapse_padding=True)

        # Add columns for label, status and reason
        table.add_column("Label", style=f"bold {COLOR}")
        table.add_column("Status", justify="right")
        table.add_column("Reason", justify="left")

        # Add rows with reasons
        table.add_row(
            "Net Zero",
            FormatString.from_bool(health_data.net_zero.status),
            health_data.net_zero.reason,
        )
        table.add_row(
            "Hedger",
            FormatString.from_bool(health_data.hedger.status),
            health_data.hedger.reason,
        )
        table.add_row(
            "Within Max Position",
            FormatString.from_bool(health_data.within_max_position.status),
            health_data.within_max_position.reason,
        )
        table.add_row(
            "Within Stop Loss",
            FormatString.from_bool(health_data.within_stop_loss.status),
            health_data.within_stop_loss.reason,
        )
        table.add_row(
            "WS Hedge",
            FormatString.from_bool(health_data.websockets.hedge.status),
            health_data.websockets.hedge.reason,
        )
        table.add_row(
            "WS Quote",
            FormatString.from_bool(health_data.websockets.quote.status),
            health_data.websockets.quote.reason,
        )

        return Panel(
            table,
            title=f"[bold {COLOR}]Health Status[/bold {COLOR}]",
            border_style=COLOR,
            expand=True,
            highlight=True,
            safe_box=True,
        )
