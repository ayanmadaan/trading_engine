from dataclasses import dataclass
from typing import Any

import rich.box as box
from rich.panel import Panel
from rich.table import Table

from watch_trading.config.panel_colors import PANEL_COLORS
from watch_trading.renderers import BaseRenderer
from watch_trading.utils import FormatString

COLOR = PANEL_COLORS["cooldown"]

JSONData = dict[str, Any]


@dataclass
class RateLimiterData:
    is_in_cooldown: bool
    remaining_cooldown_time: str

    @staticmethod
    def from_dict(data: JSONData) -> "RateLimiterData":
        return RateLimiterData(
            is_in_cooldown=bool(data["is_in_cooldown"]),
            remaining_cooldown_time=str(data["remaining_cooldown_time"]),
        )


@dataclass
class ExchangeStabilityData:
    cooldown_end_time: str
    is_in_cooldown: bool
    remaining_cooldown_time: str

    @staticmethod
    def from_dict(data: JSONData) -> "ExchangeStabilityData":
        return ExchangeStabilityData(
            cooldown_end_time=str(data["cooldown_end_time"]),
            is_in_cooldown=bool(data["is_in_cooldown"]),
            remaining_cooldown_time=str(data["remaining_cooldown_time"]),
        )


@dataclass
class CooldownData:
    exchange_stability: ExchangeStabilityData
    rate_limiters: dict[str, RateLimiterData]

    @staticmethod
    def from_dict(data: JSONData) -> "CooldownData":
        return CooldownData(
            exchange_stability=ExchangeStabilityData.from_dict(
                data["exchange_stability"]
            ),
            rate_limiters={
                name: RateLimiterData.from_dict(limiter_data)
                for name, limiter_data in data["rate_limiters"].items()
            },
        )


class CooldownRenderer(BaseRenderer):

    @classmethod
    def render(cls, data: JSONData) -> Panel:
        cooldown_data = CooldownData.from_dict(data)

        table = Table(
            show_header=False, box=None, padding=(0, 1), collapse_padding=True
        )

        # Add columns with headers
        table.add_column("Type", style=f"bold {COLOR}")
        table.add_column("Free", justify="center")
        table.add_column("Remaining", justify="right")

        # Add all rows
        table.add_row(
            "Exchange Stability",
            FormatString.from_bool(not cooldown_data.exchange_stability.is_in_cooldown),
            cooldown_data.exchange_stability.remaining_cooldown_time,
        )
        table.add_row(
            "Submit Rate Limit",
            FormatString.from_bool(
                not cooldown_data.rate_limiters["order_send"].is_in_cooldown
            ),
            cooldown_data.rate_limiters["order_send"].remaining_cooldown_time,
        )
        table.add_row(
            "Cancel Rate Limit",
            FormatString.from_bool(
                not cooldown_data.rate_limiters["order_cancel"].is_in_cooldown
            ),
            cooldown_data.rate_limiters["order_cancel"].remaining_cooldown_time,
        )
        table.add_row(
            "Modify Rate Limit",
            FormatString.from_bool(
                not cooldown_data.rate_limiters["order_modification"].is_in_cooldown
            ),
            cooldown_data.rate_limiters["order_modification"].remaining_cooldown_time,
        )

        return Panel(
            table,
            title=f"[bold {COLOR}]Cooldowns[/bold {COLOR}]",
            border_style=COLOR,
            expand=True,
            highlight=True,
            safe_box=True,
        )
