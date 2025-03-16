from dataclasses import dataclass
from typing import Any

from rich import box
from rich.console import Group
from rich.panel import Panel
from rich.table import Table

from watch_trading.config.panel_colors import PANEL_COLORS
from watch_trading.renderers import BaseRenderer
from watch_trading.utils import FormatString

COLOR = PANEL_COLORS["order_management"]

JSONData = dict[str, Any]


@dataclass
class OrderSideData:
    count: int
    orders: str
    dirty: bool = False

    @staticmethod
    def from_dict(data: JSONData) -> "OrderSideData":
        return OrderSideData(
            count=int(data["count"]),
            orders=str(data["orders"]),
            dirty=bool(data.get("dirty", False)),
        )


@dataclass
class PendingCountsData:
    cancellations: int
    modifications: int
    submissions: int

    @staticmethod
    def from_dict(data: JSONData) -> "PendingCountsData":
        return PendingCountsData(
            cancellations=int(data["cancellations"]),
            modifications=int(data["modifications"]),
            submissions=int(data["submissions"]),
        )


@dataclass
class OrderManagementData:
    live_orders: dict[str, OrderSideData]
    target_orders: dict[str, OrderSideData]
    pending_counts: PendingCountsData

    @staticmethod
    def from_dict(data: JSONData) -> "OrderManagementData":
        return OrderManagementData(
            live_orders={
                side: OrderSideData.from_dict(side_data)
                for side, side_data in data["live_orders"].items()
            },
            target_orders={
                side: OrderSideData.from_dict(side_data)
                for side, side_data in data["target_orders"].items()
            },
            pending_counts=PendingCountsData.from_dict(data["pending_counts"]),
        )


class OrderManagementRenderer(BaseRenderer):

    @classmethod
    def render(cls, data: JSONData) -> Panel:
        order_data = OrderManagementData.from_dict(data["order_management"])

        # Table 1: Order Counts
        count_table = Table(show_header=False, box=None)
        count_table.add_column("Label", style=f"bold {COLOR}")
        count_table.add_column("Value", justify="right")
        count_table.add_row("Live Ask Count", str(order_data.live_orders["ask"].count))
        count_table.add_row("Live Bid Count", str(order_data.live_orders["bid"].count))
        count_table.add_row(
            "Target Ask Count", str(order_data.target_orders["ask"].count)
        )
        count_table.add_row(
            "Target Bid Count", str(order_data.target_orders["bid"].count)
        )

        # Table 2: Order Status
        status_table = Table(show_header=False, box=None)
        status_table.add_column("Label", style=f"bold {COLOR}")
        status_table.add_column("Status", justify="right")
        status_table.add_row(
            "Target Ask Clean",
            FormatString.from_bool(not order_data.target_orders["ask"].dirty),
        )
        status_table.add_row(
            "Target Bid Clean",
            FormatString.from_bool(not order_data.target_orders["bid"].dirty),
        )

        # Table 3: Order Details
        orders_table = Table(
            box=box.SIMPLE,
            show_header=True,
        )
        orders_table.add_column("Type", style=f"bold {COLOR}")
        orders_table.add_column("Price", justify="right")
        orders_table.add_column("Bps", justify="right")
        orders_table.add_column("Quantity", justify="right")

        quote_mid = data["market_data"]["quote"]["mid"]

        # Add order rows with proper parsing
        for side in ["ask", "bid"]:
            for order_type in ["live", "target"]:
                orders = getattr(order_data, f"{order_type}_orders")[side]
                parsed_orders = cls._parse_orders(orders.orders)

                # Sort orders appropriately
                parsed_orders.sort(
                    key=lambda x: x[0], reverse=True
                )  # descending for bids

                # Set price color based on side
                price_color = "red" if side == "ask" else "green"

                # Always add header row for this order type
                header = f"{order_type.title()} {side.title()}"
                if parsed_orders:
                    orders_table.add_row(
                        f"[bold {COLOR}]{header}",
                        f"[{price_color}]{parsed_orders[0][0]:.3f}[{price_color}]",
                        f"[bold {price_color}]{(parsed_orders[0][0] - quote_mid) / quote_mid * 10000:.2f}[/bold {price_color}]",
                        f"{parsed_orders[0][1]:.3f}",
                    )
                    # Add individual orders
                    for price, quantity in parsed_orders[1:]:
                        orders_table.add_row(
                            "",  # indent the order details
                            f"[{price_color}]{price:.3f}[{price_color}]",
                            f"[bold {price_color}]{(price - quote_mid) / quote_mid * 10000:.2f}[/bold {price_color}]",
                            f"{quantity:.3f}",
                        )
                else:
                    # Add empty header row when no orders exist
                    orders_table.add_row(f"[bold {COLOR}]{header}", "", "", "")

                # Add empty row for spacing between different order types
                if not (side == "bid" and order_type == "target"):
                    orders_table.add_row("", "", "", "")

        # Table 4: Pending Actions
        pending_table = Table(show_header=False, box=None)
        pending_table.add_column("Label", style=f"bold {COLOR}")
        pending_table.add_column("Count", justify="right")
        pending_table.add_row(
            "Pending Submissions", str(order_data.pending_counts.submissions)
        )
        pending_table.add_row(
            "Pending Cancellations", str(order_data.pending_counts.cancellations)
        )
        pending_table.add_row(
            "Pending Modifications", str(order_data.pending_counts.modifications)
        )

        # Group all tables
        group = Group(
            count_table,
            "",  # Add space
            status_table,
            "",  # Add space
            orders_table,
            "",  # Add space
            pending_table,
        )

        return Panel(
            group,
            title=f"[bold {COLOR}]Order Management[/bold {COLOR}]",
            border_style=COLOR,
            expand=True,
            highlight=True,
            safe_box=True,
        )

    @staticmethod
    def _parse_orders(order_str: str) -> list[tuple[float, float]]:
        """Parse order string into list of (price, quantity) tuples"""
        if order_str == "{}":
            return []

        orders = []
        # Remove outer braces
        order_str = order_str.strip("{}")
        if not order_str:
            return []

        # Split into individual orders
        for order in order_str.split(", "):
            # Skip empty entries
            if not order:
                continue

            # Extract order details after the order ID
            details = order.split(":{")[1].rstrip("}")

            # Parse the details using simple string operations
            details_dict = dict(item.split(":") for item in details.split(","))

            # Extract price and quantity, converting to float
            try:
                price = float(details_dict["price"])
            except (ValueError, TypeError) as e:
                raise ValueError(f"Failed to convert 'price' value '{details_dict['price']}' (type: {type(details_dict['price']).__name__}) to float") from e

            try:
                quantity = float(details_dict["quantity"])
            except (ValueError, TypeError) as e:
                raise ValueError(f"Failed to convert 'quantity' value '{details_dict['quantity']}' (type: {type(details_dict['quantity']).__name__}) to float") from e

            orders.append((price, quantity))

        return orders
