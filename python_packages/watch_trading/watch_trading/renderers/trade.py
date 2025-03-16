from dataclasses import dataclass
from typing import Any

from rich import box
from rich.console import Group
from rich.panel import Panel
from rich.table import Table

from watch_trading.config.panel_colors import PANEL_COLORS
from watch_trading.renderers import BaseRenderer

COLOR = PANEL_COLORS.get("trade_analysis", "cyan")

JSONData = dict[str, Any]


@dataclass
class CountsData:
    buy_maker: int
    buy_taker: int
    buys: int
    maker: int
    sell_maker: int
    sell_taker: int
    sells: int
    taker: int
    total: int

    @staticmethod
    def from_dict(data: JSONData) -> "CountsData":
        return CountsData(
            buy_maker=int(data["buy_maker"]),
            buy_taker=int(data["buy_taker"]),
            buys=int(data["buys"]),
            maker=int(data["maker"]),
            sell_maker=int(data["sell_maker"]),
            sell_taker=int(data["sell_taker"]),
            sells=int(data["sells"]),
            taker=int(data["taker"]),
            total=int(data["total"]),
        )


@dataclass
class PositionData:
    delta_long: float
    delta_short: float
    net_delta: float

    @staticmethod
    def from_dict(data: JSONData) -> "PositionData":
        try:
            return PositionData(
                delta_long=float(data["delta_long"]),
                delta_short=float(data["delta_short"]),
                net_delta=float(data["net_delta"]),
            )
        except ValueError as e:
            # Find which key caused the conversion error
            for key in ["delta_long", "delta_short", "net_delta"]:
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
class PricesData:
    average_buy: float
    average_sell: float
    weighted_average: float

    @staticmethod
    def from_dict(data: JSONData) -> "PricesData":
        try:
            return PricesData(
                average_buy=float(data["average_buy"]),
                average_sell=float(data["average_sell"]),
                weighted_average=float(data["weighted_average"]),
            )
        except ValueError as e:
            # Find which key caused the conversion error
            for key in ["average_buy", "average_sell", "weighted_average"]:
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
class RatiosData:
    buy_sell: float
    maker: float

    @staticmethod
    def from_dict(data: JSONData) -> "RatiosData":
        try:
            return RatiosData(
                buy_sell=float(data["buy_sell"]),
                maker=float(data["maker"])
            )
        except ValueError as e:
            # Find which key caused the conversion error
            for key in ["buy_sell", "maker"]:
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
class RiskData:
    average_trade_size: float
    largest_single_trade_value: float
    trade_size_volatility: float

    @staticmethod
    def from_dict(data: JSONData) -> "RiskData":
        try:
            return RiskData(
                average_trade_size=float(data["average_trade_size"]),
                largest_single_trade_value=float(data["largest_single_trade_value"]),
                trade_size_volatility=float(data["trade_size_volatility"]),
            )
        except ValueError as e:
            # Find which key caused the conversion error
            for key in ["average_trade_size", "largest_single_trade_value", "trade_size_volatility"]:
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
class VolumeData:
    long: float
    maker: float
    short: float
    taker: float
    total: float

    @staticmethod
    def from_dict(data: JSONData) -> "VolumeData":
        try:
            return VolumeData(
                long=float(data["long"]),
                maker=float(data["maker"]),
                short=float(data["short"]),
                taker=float(data["taker"]),
                total=float(data["total"]),
            )
        except ValueError as e:
            # Find which key caused the conversion error
            for key in ["long", "maker", "short", "taker", "total"]:
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
class TradeAnalysisData:
    counts: CountsData
    position: PositionData
    prices: PricesData
    ratios: RatiosData
    risk: RiskData
    volume: VolumeData

    @staticmethod
    def from_dict(data: JSONData) -> "TradeAnalysisData":
        try:
            # Handle nested objects with better error context
            try:
                counts = CountsData.from_dict(data["counts"])
            except KeyError:
                raise KeyError("Missing required key: 'counts'")
            except Exception as e:
                raise ValueError(f"Error in 'counts' data: {str(e)}") from e

            try:
                position = PositionData.from_dict(data["position"])
            except KeyError:
                raise KeyError("Missing required key: 'position'")
            except Exception as e:
                raise ValueError(f"Error in 'position' data: {str(e)}") from e

            try:
                prices = PricesData.from_dict(data["prices"])
            except KeyError:
                raise KeyError("Missing required key: 'prices'")
            except Exception as e:
                raise ValueError(f"Error in 'prices' data: {str(e)}") from e

            try:
                ratios = RatiosData.from_dict(data["ratios"])
            except KeyError:
                raise KeyError("Missing required key: 'ratios'")
            except Exception as e:
                raise ValueError(f"Error in 'ratios' data: {str(e)}") from e

            try:
                risk = RiskData.from_dict(data["risk"])
            except KeyError:
                raise KeyError("Missing required key: 'risk'")
            except Exception as e:
                raise ValueError(f"Error in 'risk' data: {str(e)}") from e

            try:
                volume = VolumeData.from_dict(data["volume"])
            except KeyError:
                raise KeyError("Missing required key: 'volume'")
            except Exception as e:
                raise ValueError(f"Error in 'volume' data: {str(e)}") from e

            return TradeAnalysisData(
                counts=counts,
                position=position,
                prices=prices,
                ratios=ratios,
                risk=risk,
                volume=volume,
            )
        except KeyError as e:
            # Handle missing top-level keys
            raise KeyError(f"Missing required key: {e}") from e


class TradeAnalysisRenderer(BaseRenderer):
    @classmethod
    def render(cls, data: JSONData) -> Panel:
        quote_data = TradeAnalysisData.from_dict(data["quote"])
        hedge_data = TradeAnalysisData.from_dict(data["hedge"])

        # Create tables for different sections
        counts_table = cls._create_counts_table(quote_data, hedge_data)
        position_table = cls._create_position_table(quote_data, hedge_data)
        prices_table = cls._create_prices_table(quote_data, hedge_data)
        volume_table = cls._create_volume_table(quote_data, hedge_data)
        risk_table = cls._create_risk_table(quote_data, hedge_data)
        ratios_table = cls._create_ratios_table(quote_data, hedge_data)

        left_stack = Group(counts_table, "", position_table, "", prices_table)

        right_stack = Group(volume_table, "", risk_table, "", ratios_table)
        # Create a container table to hold both tables side by side
        container = Table.grid(padding=0)
        container.add_column()
        container.add_column()
        container.add_row(left_stack, right_stack)

        return Panel(
            container,
            title=f"[bold {COLOR}]Trade Analysis[/bold {COLOR}]",
            border_style=COLOR,
            expand=True,
            highlight=True,
            safe_box=True,
        )

    @classmethod
    def _create_counts_table(
        cls, quote: TradeAnalysisData, hedge: TradeAnalysisData
    ) -> Table:
        table = Table(show_header=True, box=box.SIMPLE, collapse_padding=True)
        table.add_column("Counts", style=f"bold {COLOR}")
        table.add_column("Quote", justify="right")
        table.add_column("Hedge", justify="right")

        table.add_row("Total", str(quote.counts.total), str(hedge.counts.total))
        table.add_row("Buys", str(quote.counts.buys), str(hedge.counts.buys))
        table.add_row("Sells", str(quote.counts.sells), str(hedge.counts.sells))
        table.add_row("Maker", str(quote.counts.maker), str(hedge.counts.maker))
        table.add_row("Taker", str(quote.counts.taker), str(hedge.counts.taker))
        return table

    @classmethod
    def _create_position_table(
        cls, quote: TradeAnalysisData, hedge: TradeAnalysisData
    ) -> Table:
        table = Table(show_header=True, box=box.SIMPLE, collapse_padding=True)
        table.add_column("Position", style=f"bold {COLOR}")
        table.add_column("Quote", justify="right")
        table.add_column("Hedge", justify="right")

        table.add_row(
            "Delta Long",
            f"{quote.position.delta_long:.3f}",
            f"{hedge.position.delta_long:.3f}",
        )
        table.add_row(
            "Delta Short",
            f"{quote.position.delta_short:.3f}",
            f"{hedge.position.delta_short:.3f}",
        )
        table.add_row(
            "Net Delta",
            f"{quote.position.net_delta:.3f}",
            f"{hedge.position.net_delta:.3f}",
        )
        return table

    @classmethod
    def _create_prices_table(
        cls, quote: TradeAnalysisData, hedge: TradeAnalysisData
    ) -> Table:
        table = Table(show_header=True, box=box.SIMPLE, collapse_padding=True)
        table.add_column("Prices", style=f"bold {COLOR}")
        table.add_column("Quote", justify="right")
        table.add_column("Hedge", justify="right")

        table.add_row(
            "Avg Buy",
            f"{quote.prices.average_buy:.1f}",
            f"{hedge.prices.average_buy:.1f}",
        )
        table.add_row(
            "Avg Sell",
            f"{quote.prices.average_sell:.1f}",
            f"{hedge.prices.average_sell:.1f}",
        )
        table.add_row(
            "VWAP",
            f"{quote.prices.weighted_average:.1f}",
            f"{hedge.prices.weighted_average:.1f}",
        )
        return table

    @classmethod
    def _create_volume_table(
        cls, quote: TradeAnalysisData, hedge: TradeAnalysisData
    ) -> Table:
        table = Table(show_header=True, box=box.SIMPLE, collapse_padding=True)
        table.add_column("Volume", style=f"bold {COLOR}")
        table.add_column("Quote", justify="right")
        table.add_column("Hedge", justify="right")

        table.add_row("Total", f"{quote.volume.total:.3f}", f"{hedge.volume.total:.3f}")
        table.add_row("Long", f"{quote.volume.long:.3f}", f"{hedge.volume.long:.3f}")
        table.add_row("Short", f"{quote.volume.short:.3f}", f"{hedge.volume.short:.3f}")
        table.add_row("Maker", f"{quote.volume.maker:.3f}", f"{hedge.volume.maker:.3f}")
        table.add_row("Taker", f"{quote.volume.taker:.3f}", f"{hedge.volume.taker:.3f}")
        return table

    @classmethod
    def _create_risk_table(
        cls, quote: TradeAnalysisData, hedge: TradeAnalysisData
    ) -> Table:
        table = Table(show_header=True, box=box.SIMPLE, collapse_padding=True)
        table.add_column("Risk", style=f"bold {COLOR}")
        table.add_column("Quote", justify="right")
        table.add_column("Hedge", justify="right")

        table.add_row(
            "Largest Trade",
            f"{quote.risk.largest_single_trade_value:.3f}",
            f"{hedge.risk.largest_single_trade_value:.3f}",
        )
        table.add_row(
            "Avg Size",
            f"{quote.risk.average_trade_size:.3f}",
            f"{hedge.risk.average_trade_size:.3f}",
        )
        table.add_row(
            "Size Vol",
            f"{quote.risk.trade_size_volatility:.3f}",
            f"{hedge.risk.trade_size_volatility:.3f}",
        )
        return table

    @classmethod
    def _create_ratios_table(
        cls, quote: TradeAnalysisData, hedge: TradeAnalysisData
    ) -> Table:
        table = Table(show_header=True, box=box.SIMPLE, collapse_padding=True)
        table.add_column("Ratios", style=f"bold {COLOR}")
        table.add_column("Quote", justify="right")
        table.add_column("Hedge", justify="right")

        table.add_row(
            "Buy/Sell", f"{quote.ratios.buy_sell:.3f}", f"{hedge.ratios.buy_sell:.3f}"
        )
        table.add_row("Maker", f"{quote.ratios.maker:.3f}", f"{hedge.ratios.maker:.3f}")
        return table
