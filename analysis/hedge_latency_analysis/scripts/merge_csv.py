#!/usr/bin/env python3
import csv
import sys


def merge_csv(input_files, output_file):
    """
    Merge CSV files provided in the list `input_files` into one file.
    The header from the first file is written and subsequent files have their header skipped.
    """
    with open(output_file, "w", newline="") as out_csv:
        writer = None
        for file in input_files:
            with open(file, "r", newline="") as in_csv:
                reader = csv.reader(in_csv)
                try:
                    header = next(reader)
                except StopIteration:
                    # Skip empty files
                    continue
                if writer is None:
                    writer = csv.writer(out_csv)
                    writer.writerow(header)
                for row in reader:
                    writer.writerow(row)
    print(f"Merged CSV created as '{output_file}'.")


if __name__ == "__main__":
    # Ensure at least two input files and one output file are provided.
    if len(sys.argv) < 4:
        print("Usage: merge_csv.py input1.csv input2.csv ... output.csv")
        sys.exit(1)
    # All arguments except the last are input CSV files
    input_files = sys.argv[1:-1]
    # The last argument is the output file path
    output_file = sys.argv[-1]
    merge_csv(input_files, output_file)
