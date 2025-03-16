from turtle import filling
import pandas as pd
import numpy as np
from datetime import datetime
from bisect import bisect_left
import os
import json
from pprint import pprint


class MarketDataManager:
    def __init__(self, data_file):
        # Check if file is NPZ or CSV
        file_ext = os.path.splitext(data_file)[1].lower()

        if file_ext == ".npz":
            self._load_from_npz(data_file)
        elif file_ext == ".csv":
            self._load_from_csv(data_file)
        else:
            raise ValueError(f"Unsupported file format: {file_ext}. Use .csv or .npz")

    def _load_from_npz(self, npz_file):
        # Load arrays from NPZ file
        data = np.load(npz_file)
        self.ask_timestamps = data["ask_timestamps"]
        self.ask_prices = data["ask_prices"]
        self.bid_timestamps = data["bid_timestamps"]
        self.bid_prices = data["bid_prices"]

    def _load_from_csv(self, csv_file):
        self.data_file = csv_file
        self.df = pd.read_csv(csv_file)

        # Create separate dataframes for ask and bid
        self.df_ask = self.df[self.df["side"] == 1]
        self.df_bid = self.df[self.df["side"] == 0]

        # Sort once during initialization
        self.df_ask = self.df_ask.sort_values(by="exchangeTimestamp")
        self.df_bid = self.df_bid.sort_values(by="exchangeTimestamp")

        # Pre-compute numpy arrays for faster access
        self.ask_timestamps = self.df_ask["exchangeTimestamp"].values
        self.ask_prices = self.df_ask["price"].values
        self.bid_timestamps = self.df_bid["exchangeTimestamp"].values
        self.bid_prices = self.df_bid["price"].values

    def get_mid(self, timestamp_str):
        timestamp_dt = datetime.fromisoformat(timestamp_str)
        timestamp_ns = int(timestamp_dt.timestamp() * 1e9)

        # Calculate ask_price and bid_price
        ask_price = self._get_interpolated_price_fast(
            timestamp_ns, self.ask_timestamps, self.ask_prices
        )
        bid_price = self._get_interpolated_price_fast(
            timestamp_ns, self.bid_timestamps, self.bid_prices
        )

        return (bid_price + ask_price) / 2

    def _get_interpolated_price_fast(self, timestamp_ns, timestamps, prices):
        # Check if timestamp exists in array
        exact_match_idx = np.searchsorted(timestamps, timestamp_ns)
        if (
            exact_match_idx < len(timestamps)
            and timestamps[exact_match_idx] == timestamp_ns
        ):
            return prices[exact_match_idx]

        # Find the index for insertion (where the timestamp would go)
        idx = bisect_left(timestamps, timestamp_ns)

        # Check bounds
        if idx == 0 or idx >= len(timestamps):
            raise ValueError(f"Cannot interpolate price for timestamp {timestamp_ns}")

        # Get before and after indices
        before_idx = idx - 1
        after_idx = idx

        # Get timestamps and prices
        before_ts = timestamps[before_idx]
        after_ts = timestamps[after_idx]
        before_price = prices[before_idx]
        after_price = prices[after_idx]

        # Linear interpolation
        ratio = (timestamp_ns - before_ts) / (after_ts - before_ts)
        price = before_price + (after_price - before_price) * ratio

        return price


from datetime import datetime, timedelta


def add_ms(timestamp: str, ms: int) -> str:
    dt = datetime.strptime(timestamp, "%Y-%m-%dT%H:%M:%S.%f")
    dt = dt + timedelta(milliseconds=ms)
    return dt.strftime("%Y-%m-%dT%H:%M:%S.%f")


def get_time_diff_ms(timestamp1: str, timestamp2: str) -> int:
    dt1 = datetime.strptime(timestamp1, "%Y-%m-%dT%H:%M:%S.%f")
    dt2 = datetime.strptime(timestamp2, "%Y-%m-%dT%H:%M:%S.%f")
    return int((dt2 - dt1).total_seconds() * 1000)


if __name__ == "__main__":
    # Use NPZ file path
    data = "/home/jack/jackmm/okx_analysis/data/market_data.npz"

    mdm = MarketDataManager(data)

    loss_hg_file = "/home/jack/jackmm/okx_analysis/output/loss_hg_compact.json"
    with open(loss_hg_file, "r") as f:
        groups = json.load(f)

    # Loop through each group in the loss_hg_compact.json file
    for group in groups:
        hg_id = group["hg_id"]
        pnl_with_fee = group["pnl_with_fee"]

        # Loop through each order in the group
        for oid, fills in group["orders"].items():

            # Loop through each fill in the order
            for fill in fills:
                send_time = fill["send"]
                fill_time = fill["fill"]
                mid_price_send_time = mdm.get_mid(send_time)
                mid_price_fill_time = fill["price"]
                fill["mid_price_send_time"] = mid_price_send_time
                fill["mid_price_fill_time"] = mid_price_fill_time

                send_time_add_20ms = add_ms(send_time, 20)
                mid_price_send_time_add_20ms = mdm.get_mid(send_time_add_20ms)
                fill["mid_price_send_time_add_20ms"] = mid_price_send_time_add_20ms
                send_time_add_100ms = add_ms(send_time, 100)
                mid_price_send_time_add_100ms = mdm.get_mid(send_time_add_100ms)
                fill["mid_price_send_time_add_100ms"] = mid_price_send_time_add_100ms
                fill["send_to_fill_time_ms"] = get_time_diff_ms(send_time, fill_time)

                # Calculate pnl change
                quantity = fill["quantity"]
                if fill["side"] == "sell":
                    pnl_change = (
                        mid_price_send_time_add_20ms - mid_price_fill_time
                    ) * quantity
                elif fill["side"] == "buy":
                    pnl_change = (
                        mid_price_fill_time - mid_price_send_time_add_20ms
                    ) * quantity

                fill["pnl_change"] = pnl_change

                # Calculate t_0_20_bps and t_0_20_bps_cost
                t_0_20_bps = (
                    (mid_price_send_time_add_20ms - mid_price_send_time)
                    / mid_price_send_time
                    * 10000
                )
                t_0_20_bps_cost = t_0_20_bps
                if (fill["side"] == "buy" and t_0_20_bps > 0) or (
                    fill["side"] == "sell" and t_0_20_bps < 0
                ):
                    t_0_20_bps_cost = abs(t_0_20_bps)
                else:
                    t_0_20_bps_cost = -1 * abs(t_0_20_bps)
                fill["t_0_20_bps"] = t_0_20_bps
                fill["t_0_20_bps_cost"] = t_0_20_bps_cost

                # Calculate t_0_100_bps and t_0_100_bps_cost
                t_0_100_bps = (
                    (mid_price_send_time_add_100ms - mid_price_send_time)
                    / mid_price_send_time
                    * 10000
                )
                t_0_100_bps_cost = t_0_100_bps
                if (fill["side"] == "buy" and t_0_100_bps > 0) or (
                    fill["side"] == "sell" and t_0_100_bps < 0
                ):
                    t_0_100_bps_cost = abs(t_0_100_bps)
                else:
                    t_0_100_bps_cost = -1 * abs(t_0_100_bps)
                fill["t_0_100_bps"] = t_0_100_bps
                fill["t_0_100_bps_cost"] = t_0_100_bps_cost

                # Calculate t_20_100_bps and t_20_100_bps_cost
                t_20_100_bps = (
                    (mid_price_send_time_add_100ms - mid_price_send_time_add_20ms)
                    / mid_price_send_time_add_20ms
                    * 10000
                )
                t_20_100_bps_cost = t_20_100_bps
                if (fill["side"] == "buy" and t_20_100_bps > 0) or (
                    fill["side"] == "sell" and t_20_100_bps < 0
                ):
                    t_20_100_bps_cost = abs(t_20_100_bps)
                else:
                    t_20_100_bps_cost = -1 * abs(t_20_100_bps)
                fill["t_20_100_bps"] = t_20_100_bps
                fill["t_20_100_bps_cost"] = t_20_100_bps_cost

    for group in groups:
        pnl_with_fee = float(group["pnl_with_fee"])
        total_pnl_change = 0
        for oid, fills in group["orders"].items():
            for fill in fills:
                total_pnl_change += fill["pnl_change"]
        group["total_pnl_change"] = total_pnl_change
        expected_pnl = pnl_with_fee + total_pnl_change
        group["expected_pnl"] = expected_pnl
        group["turn_positive"] = bool(expected_pnl > 0)

    # # write to output/loss_hg_compact_with_pnl.json
    with open("/home/jack/jackmm/okx_analysis/output/pnl_analysis.json", "w") as f:
        json.dump(groups, f, indent=4)
