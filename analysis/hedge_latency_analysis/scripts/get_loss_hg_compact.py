import json
from pprint import pprint

input_file = "/home/jack/jackmm/okx_analysis/output/loss_hg.json"
output_file = "/home/jack/jackmm/okx_analysis/output/loss_hg_compact.json"
with open(input_file, "r") as f:
    loss_hg_groups = json.load(f)


def process_group(group):
    processed_group = {}
    for oid, order in group["orders"].items():
        if order["venue_role"] == "hedge":
            processed_group["hg_id"] = group["id"]
            processed_group["pnl_with_fee"] = group["pnl"]["pnl_with_fee"]
            processed_group["orders"] = {}
            processed_group["orders"][oid] = []
            for fill in order["fills"]:
                send_time = order["events"]["send_time_oms"]
                fill_time = fill["exchange_fill_time"]
                tx_id = fill["transaction_id"]
                price = fill["price"]
                quantity = fill["quantity"]
                processed_group["orders"][oid].append(
                    {
                        "tx_id": tx_id,
                        "send": send_time,
                        "fill": fill_time,
                        "side": order["side"],
                        "price": price,
                        "quantity": quantity,
                    }
                )

    return processed_group


result = []
for group in loss_hg_groups:
    hedge_order_timestamps = process_group(group)
    result.append(hedge_order_timestamps)

with open(output_file, "w") as f:
    json.dump(result, f, indent=4)
