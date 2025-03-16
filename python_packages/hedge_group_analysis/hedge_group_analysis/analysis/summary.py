import json
from tabulate import tabulate
from colorama import Fore, Style, init

# Initialize colorama
init()


class SummaryAnalysis:
    """Analyzes hedge group entries and creates a summary table"""

    def __init__(self):
        self.entries = []
        self.table_data = []

    def load_entries(self, log_entries):
        """Load log entries for analysis"""
        self.entries = log_entries
        return self

    def _extract_data(self, entry):
        """Extract relevant data from a log entry"""
        try:
            # Extract JSON from log entry message
            if hasattr(entry, "message"):
                json_string = entry.message.split(" ")[2]
                data = json.loads(json_string)
            else:
                # Assume entry is already JSON data
                data = entry

            # Extract required fields
            pnl = data.get("pnl", {})

            # Count orders by role
            orders = data.get("orders", {})
            n_quote = sum(
                1 for order in orders.values() if order.get("venue_role") == "quote"
            )
            n_hedge = sum(
                1 for order in orders.values() if order.get("venue_role") == "hedge"
            )

            # Calculate quantities
            quote_qty = sum(
                order.get("filled_quantity", 0)
                for order in orders.values()
                if order.get("venue_role") == "quote"
            )
            hedge_qty = sum(
                order.get("filled_quantity", 0)
                for order in orders.values()
                if order.get("venue_role") == "hedge"
            )

            # Parse ID to get just the numeric part
            id_parts = data["id"].split("_")
            hg_id = id_parts[-1] if len(id_parts) > 1 else data["id"]

            # Convert duration from microseconds to milliseconds
            duration_ms = data.get("duration_us", 0) / 1000

            # Determine win status with color
            is_win = pnl.get("pnl_with_fee", 0) > 0
            win_text = "Win" if is_win else "Loss"
            # Store both the text and the win status for later coloring
            win = {"text": win_text, "is_win": is_win}

            return {
                "id": hg_id,
                "duration": duration_ms,
                "win": win,
                "pnl_with_fee": pnl.get("pnl_with_fee", 0),
                "pnl_without_fee": pnl.get("pnl_without_fee", 0),
                "maker_fee": pnl.get("maker_fee", 0),
                "taker_fee": pnl.get("taker_fee", 0),
                "total_fee": pnl.get("total_fee", 0),
                "n_quote": n_quote,
                "n_hedge": n_hedge,
                "quote_qty": quote_qty,
                "hedge_qty": hedge_qty,
            }
        except Exception as e:
            print(f"Error processing entry: {e}")
            return None

    def analyze(self):
        """Process entries and prepare table data"""
        self.table_data = []

        for entry in self.entries:
            row_data = self._extract_data(entry)
            if row_data:
                self.table_data.append(row_data)

        return self

    def calculate_win_ratios(self):
        """Calculate win ratio and normalized win ratio"""
        if not self.table_data:
            return 0, 0

        # Count number of wins
        wins = sum(1 for data in self.table_data if data["win"]["is_win"])
        total_trades = len(self.table_data)

        # Calculate simple win ratio (percentage of winning trades)
        win_ratio = wins / total_trades if total_trades > 0 else 0

        # Calculate PnL sums for winning and losing trades
        win_pnl_sum = sum(
            data["pnl_with_fee"] for data in self.table_data if data["win"]["is_win"]
        )
        loss_pnl_sum = abs(
            sum(
                data["pnl_with_fee"]
                for data in self.table_data
                if not data["win"]["is_win"]
            )
        )

        # Calculate normalized win ratio
        total_abs_pnl = win_pnl_sum + loss_pnl_sum
        normalized_win_ratio = win_pnl_sum / total_abs_pnl if total_abs_pnl > 0 else 0

        return win_ratio, normalized_win_ratio

    def get_top_loss_trades(self, n=3):
        """Return the top N loss trades sorted by PnL (worst first)"""
        # Filter for loss trades and sort by PnL (ascending to get worst losses first)
        loss_trades = [data for data in self.table_data if not data["win"]["is_win"]]
        top_losses = sorted(loss_trades, key=lambda x: x["pnl_with_fee"])[:n]
        return top_losses

    def report(self):
        """Generate and print the summary table"""
        if not self.table_data:
            print("No data available for analysis.")
            return

        # Define headers
        headers = [
            "ID",
            "Duration (ms)",
            "Win",
            "PnL w/ Fee",
            "PnL w/o Fee",
            "Maker Fee",
            "Taker Fee",
            "Total Fee",
            "# Quote",
            "# Hedge",
            "Quote Qty",
            "Hedge Qty",
        ]

        # Prepare rows with formatted values
        rows = []
        for data in self.table_data:
            # Apply color to Win/Loss text
            win_value = data["win"]
            if win_value["is_win"]:
                colored_win = f"{Fore.GREEN}{win_value['text']}{Style.RESET_ALL}"
            else:
                colored_win = f"{Fore.RED}{win_value['text']}{Style.RESET_ALL}"

            row = [
                data["id"],
                f"{data['duration']:.2f}",
                colored_win,
                f"{data['pnl_with_fee']:.6f}",
                f"{data['pnl_without_fee']:.6f}",
                f"{data['maker_fee']:.6f}",
                f"{data['taker_fee']:.6f}",
                f"{data['total_fee']:.6f}",
                data["n_quote"],
                data["n_hedge"],
                f"{data['quote_qty']:.6f}",
                f"{data['hedge_qty']:.6f}",
            ]
            rows.append(row)

        # Calculate summary row (sum of all rows)
        sum_row = ["TOTAL", "-"]

        # Calculate total PnL with fee for determining win/loss
        total_pnl_with_fee = sum(data["pnl_with_fee"] for data in self.table_data)

        # Determine total win/loss status
        if total_pnl_with_fee > 0:
            total_win = f"{Fore.GREEN}Win{Style.RESET_ALL}"
        elif total_pnl_with_fee < 0:
            total_win = f"{Fore.RED}Loss{Style.RESET_ALL}"
        else:
            total_win = "-"

        sum_row.append(total_win)

        # Sum numeric columns (starting from pnl_with_fee onward)
        for col_idx in range(3, 12):
            # Skip the win column which is not numeric
            if col_idx in [3, 4, 5, 6, 7, 8, 9, 10, 11]:
                col_key = [
                    "pnl_with_fee",
                    "pnl_without_fee",
                    "maker_fee",
                    "taker_fee",
                    "total_fee",
                    "n_quote",
                    "n_hedge",
                    "quote_qty",
                    "hedge_qty",
                ][col_idx - 3]

                # Sum all numeric values
                total = sum(data[col_key] for data in self.table_data)

                # Format based on column type
                if col_key in ["n_quote", "n_hedge"]:
                    # Integer formatting
                    sum_row.append(str(total))
                else:
                    # Float formatting with 6 decimal places
                    sum_row.append(f"{total:.6f}")

        # Add summary row to the table
        rows.append(sum_row)

        # Generate and print the table
        print(tabulate(rows, headers=headers, tablefmt="simple_grid"))

        # Calculate and print win ratios
        win_ratio, normalized_win_ratio = self.calculate_win_ratios()
        print("\nPerformance Metrics:")
        print(f"Number of Hedge Groups: {len(self.table_data)}")
        print(f"1. Win Ratio: {win_ratio:.2%}")
        print(f"2. Normalized Win Ratio: {normalized_win_ratio:.2%}")

        # Print top 10 loss trades
        top_losses = self.get_top_loss_trades(10)
        if top_losses:
            print("\nTop 10 Loss Trades:")
            total_loss = abs(
                sum(
                    data["pnl_with_fee"]
                    for data in self.table_data
                    if not data["win"]["is_win"]
                )
            )

            # Prepare table data for tabulate
            loss_headers = ["#", "Hedge Group ID", "Loss (PnL w/ Fee)", "Relative Loss"]
            loss_rows = []

            for i, loss in enumerate(top_losses, 1):
                # Calculate relative loss (percentage of total loss)
                relative_loss = (
                    abs(loss["pnl_with_fee"]) / total_loss if total_loss > 0 else 0
                )

                loss_rows.append(
                    [
                        i,
                        loss["id"],
                        f"{loss['pnl_with_fee']:.6f}",
                        f"{relative_loss:.2%}",
                    ]
                )

            # Print the tabulated loss table
            print(tabulate(loss_rows, headers=loss_headers, tablefmt="simple_grid"))
