import pandas as pd
import numpy as np
import time
import os
import sys


def convert_csv_to_npz(csv_file, output_dir=None):
    # Create output directory if it doesn't exist
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir)
        print(f"Created output directory: {output_dir}")

    # Get the base filename without path and extension
    base_filename = os.path.basename(csv_file)
    base_name = os.path.splitext(base_filename)[0]

    # Determine output file path
    if output_dir:
        npz_file = os.path.join(output_dir, base_name + ".npz")
    else:
        npz_file = os.path.splitext(csv_file)[0] + ".npz"

    print(f"Converting {csv_file} to NPZ format...")
    start = time.time()

    # Load the CSV
    df = pd.read_csv(csv_file)

    # Process the data
    df_ask = df[df["side"] == 1]
    df_bid = df[df["side"] == 0]
    df_ask = df_ask.sort_values(by="exchangeTimestamp")
    df_bid = df_bid.sort_values(by="exchangeTimestamp")

    # Extract numpy arrays
    ask_timestamps = df_ask["exchangeTimestamp"].values
    ask_prices = df_ask["price"].values
    bid_timestamps = df_bid["exchangeTimestamp"].values
    bid_prices = df_bid["price"].values

    # Save to NPZ file
    np.savez(
        npz_file,
        ask_timestamps=np.array(ask_timestamps),
        ask_prices=np.array(ask_prices),
        bid_timestamps=np.array(bid_timestamps),
        bid_prices=np.array(bid_prices),
    )

    duration = time.time() - start
    print(f"Conversion completed in {duration:.4f} seconds")
    print(f"NPZ file saved to: {npz_file}")
    print(f"Data points - Ask: {len(ask_timestamps)}, Bid: {len(bid_timestamps)}")

    return npz_file


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python script.py <csv_file> [output_directory]")
        sys.exit(1)

    csv_file = sys.argv[1]

    # Check if output directory was provided
    output_dir = "/home/jack/jackmm/okx_analysis/data"
    if len(sys.argv) > 2:
        output_dir = sys.argv[2]

    convert_csv_to_npz(csv_file, output_dir)
