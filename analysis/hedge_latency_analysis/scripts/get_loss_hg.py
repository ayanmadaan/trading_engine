input_file = "/home/jack/jackmm/okx_analysis/data/hg_data_all.txt"
output_file = "/home/jack/jackmm/okx_analysis/output/loss_hg.json"

import json

loss_hedge_groups = []

# Define a placeholder function for get_message
# This function should be implemented based on the actual logic needed
# For now, it will just return the line as a JSON object


def get_message(line):
    # Extract the JSON part of the line
    json_str = line.split("|")[-1].strip().split(" ")[-1]
    # Parse the JSON string
    return json.loads(json_str)


with open(input_file, "r") as f:
    for line in f:
        # Assuming each line is a JSON string
        msg = get_message(line.strip())
        is_loss = float(msg["pnl"]["pnl_with_fee"]) < 0
        if is_loss:
            loss_hedge_groups.append(msg)

# Write the win_hedge_groups to the output file
with open(output_file, "w") as f:
    json.dump(loss_hedge_groups, f, indent=4)
