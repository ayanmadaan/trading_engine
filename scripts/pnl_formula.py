import argparse

# ANSI escape code for green color
GREEN = "\033[32m"
RESET = "\033[0m"


# Function to print in green
def print_green(text):
    print(f"{GREEN}{text}{RESET}")


# Parse command line arguments
parser = argparse.ArgumentParser(description="Calculate minimum quote spread")
parser.add_argument("--hedge-spread", type=float, help="Hedge spread")
parser.add_argument("--hedge-spread-bps", type=float, help="Hedge spread in bps")
parser.add_argument("--pnl", type=float, help="PNL")
parser.add_argument("--pnl-bps", type=float, help="PNL in bps")
parser.add_argument("--qty", type=float, help="Quantity", required=True)
parser.add_argument(
    "--keep", type=float, help="PNL keep ratio (between 0 and 1)", required=True
)

args = parser.parse_args()

if args.hedge_spread is None and args.hedge_spread_bps is None:
    parser.error("At least one of --hedge-spread or --hedge-spread-bps must be set")

if args.pnl is None and args.pnl_bps is None:
    parser.error("At least one of --pnl or --pnl-bps must be set")

if args.keep < 0 or args.keep > 1:
    parser.error("PNL keep ratio must be between 0 and 1")

# Fixed parameters
capital = 5000  # USD
quote_fee = -0.25e-4  # maker fee
hedge_fee = 3e-4  # 0.8 bps maker and 3 bps taker fee

# Estimated parameters
asset_price = 100e3  # USD
cover_range = 20e-4

# Variables from command line arguments
hedge_spread_bps = args.hedge_spread_bps
hedge_spread = (
    args.hedge_spread if hedge_spread_bps is None else hedge_spread_bps * 1e-4
)
qty = args.qty
pnl_bps = args.pnl_bps
pnl = args.pnl if pnl_bps is None else pnl_bps * 1e-4
keep = args.keep

# Derived parameters
print("Computation process:\n")
print(f"1. Input parameters:")
print(f"   - Hedge spread: {hedge_spread:.2e}")
print(f"   - Quantity: {qty}")
print(f"   - Asset price: {asset_price:,.2f} USD")
print(f"   - Capital: {capital:,.2f} USD")
print(f"   - Cover range: {cover_range * 1e4:.2f} bps")

fee_cost = 2 * (quote_fee + hedge_fee)
print(f"\n2. Fee cost calculation:")
print(f"   - Quote fee: {quote_fee:.2e}")
print(f"   - Hedge fee: {hedge_fee:.2e}")
print(f"   - Total fee cost (2 * (quote_fee + hedge_fee)): {fee_cost:.2e}")

max_position = capital / asset_price
print(f"\n3. Maximum position:")
print(f"   - Max position (capital / asset_price): {max_position:.4f}")

position_shift = cover_range / max_position
print(f"\n4. Position shift:")
print(f"   - Position shift (cover_range / max_position): {position_shift:.2e}")

min_quote_spread = hedge_spread + fee_cost + position_shift * qty
min_quote_spread_bps = min_quote_spread * 1e4
min_quote_offset = min_quote_spread / 2
min_quote_offset_bps = min_quote_offset * 1e4

print(f"\n5. Min quote spread and offset:")
print(f"   - Min quote spread: {min_quote_spread:.2e}")
print(f"   - Min quote spread in bps: {min_quote_spread_bps:.2f}")
print(f"   - Min quote offset: {min_quote_offset:.2e}")
print(f"   - Min quote offset in bps: {min_quote_offset_bps:.2f}")

print("\n6. Best and worst PNL:")
print(f"   - Best PNL: {pnl:.2e}")
print(f"   - Best PNL in bps: {pnl * 1e4:.2f}")
print(f"   - Worst PNL: {pnl * keep:.2e}")
print(f"   - Worst PNL in bps: {pnl * keep * 1e4:.2f}")

suggestd_quote_offset = min_quote_offset + pnl
suggestd_quote_offset_bps = suggestd_quote_offset * 1e4
safety_margin = min_quote_offset + pnl * keep
safety_margin_bps = safety_margin * 1e4

print(f"\n7. Suggested quote offset and safety margin:")
print(f"   - Suggested quote offset: {suggestd_quote_offset:.3e}")
print_green(f"   - Suggested quote offset in bps: {suggestd_quote_offset_bps:.3f}")
print(f"   - Suggested safety margin: {safety_margin:.3e}")
print_green(f"   - Suggested safety margin in bps: {safety_margin_bps:.3f}")
