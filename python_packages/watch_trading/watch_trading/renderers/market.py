# renderers/market_renderer.py
from dataclasses import dataclass
from typing import Any
from datetime import datetime, timezone

from rich import box
from rich.table import Table

from watch_trading.renderers import BaseRenderer
from watch_trading.utils import FormatString
from watch_trading.config.panel_colors import PANEL_COLORS

COLOR = PANEL_COLORS["market_data"]

JSONData = dict[str, Any]

@dataclass
class VenueData:
    best_bid: float
    best_ask: float
    is_fresh: bool
    mid: float
    spread_bps: float
    timestamp: str
    time_diff_ms: float

    @staticmethod
    def from_dict(data: JSONData) -> 'VenueData':
        # First check for None values
        for key in ['best_bid', 'best_ask', 'mid', 'spread_bps', 'time_diff_ms']:
            if data.get(key) is None:
                raise ValueError(f"Cannot convert key '{key}' with value None to float")

        try:
            return VenueData(
                best_bid=float(data['best_bid']),
                best_ask=float(data['best_ask']),
                is_fresh=bool(data['is_fresh']),
                mid=float(data['mid']),
                spread_bps=float(data['spread_bps']),
                timestamp=str(data['timestamp']),
                time_diff_ms=float(data['time_diff_ms'])
            )
        except ValueError as e:
            # Find which key caused the conversion error
            for key in ['best_bid', 'best_ask', 'mid', 'spread_bps', 'time_diff_ms']:
                try:
                    float(data[key])
                except (ValueError, TypeError):
                    raise ValueError(f"Failed to convert '{key}' value '{data[key]}' (type: {type(data[key]).__name__}) to float") from e
            # If we get here, it's likely some other conversion error
            raise e
        except KeyError as e:
            # Handle missing keys
            raise KeyError(f"Missing required key: {e}") from e

@dataclass
class MarketData:
    reference: VenueData
    quote: VenueData
    hedge: VenueData

    @staticmethod
    def from_dict(market_data: JSONData) -> 'MarketData':
        return MarketData(
            reference=VenueData.from_dict(market_data['reference']),
            quote=VenueData.from_dict(market_data['quote']),
            hedge=VenueData.from_dict(market_data['hedge'])
        )


class MarketDataRenderer(BaseRenderer):

    @classmethod
    def render(cls, data: JSONData) -> Table:
        market_data = MarketData.from_dict(data)
        table = Table(
            box=box.ROUNDED,
            show_header=True,
            header_style=f"bold {COLOR}",
            border_style=COLOR,
        )

        # Add columns
        table.add_column("Market", style=f"bold {COLOR}")
        table.add_column("Bid", justify="right")
        table.add_column("Mid", justify="right")
        table.add_column("Ask", justify="right")
        table.add_column("Spread(bps)", justify="right")
        table.add_column("Timestamp", justify="right")
        table.add_column("Fresh", justify="center")
        table.add_column("Age(ms)", justify="right")


        # Add venue data rows
        venues: list[tuple[str, VenueData]] = [
            ("REFERENCE", market_data.reference),
            ("QUOTE", market_data.quote),
            ("HEDGE", market_data.hedge)
        ]

        for venue_name, venue_data in venues:
            age_str = f"{venue_data.time_diff_ms:.2f}"
            fresh_text = FormatString.from_bool(venue_data.is_fresh)

            table.add_row(
                venue_name,
                f"{venue_data.best_bid:.3f}",
                f"{venue_data.mid:.3f}",
                f"{venue_data.best_ask:.3f}",
                f"{venue_data.spread_bps:.2f}",
                FormatString.from_timestamp_yyyymmdd_hhmmss_micros(venue_data.timestamp),
                fresh_text,
                age_str
            )

        return table
