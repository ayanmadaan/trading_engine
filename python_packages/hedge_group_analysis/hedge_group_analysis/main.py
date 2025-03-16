import argparse
from hedge_group_analysis.analysis import DurationAnalysis, SummaryAnalysis, CorrelationAnalysis, CancelFillAnalysis
from hedge_group_analysis.utils import LogEntry, LogEntryFilter
import json

SEARCH_TERM = "hedge_group_analysis"

def parse_arguments():
    parser = argparse.ArgumentParser(description='Process log files for hedge group analysis')
    parser.add_argument('-l', '--log', type=str, required=True, help='Path to the log file to analyze')
    parser.add_argument('-s', '--summary', action='store_true', help='Analyze summary of hedge groups')
    parser.add_argument('-d', '--duration', type=int, required=False, help='Hedge group id to analyze duration')
    parser.add_argument('-c', '--correlation', action='store_true', help='Analyze correlation between hedge latency and PnL losses')
    parser.add_argument('-cf', '--cancel-fill', action='store_true', help='Analyze cancel-to-fill durations')
    return parser.parse_args()

def get_hedge_group(log_entries: list[LogEntry], hedge_group_id: int) -> LogEntry:
    for entry in log_entries:
        json_string = entry.message.split(" ")[2]
        json_object = json.loads(json_string)
        id = int(json_object["id"].split("_")[-1])
        if id == hedge_group_id:
            return entry
    raise ValueError(f"Hedge group id {hedge_group_id} not found")

def extract_hedge_group_id(entry: LogEntry) -> str:
    """Extract the hedge group ID from a log entry"""
    json_string = entry.message.split(" ")[2]
    json_object = json.loads(json_string)
    return json_object["id"].split("_")[-1]

def main():
    args = parse_arguments()
    log_filter = LogEntryFilter(args.log)
    matching_entries: list[LogEntry] = log_filter.get_matching_entries(SEARCH_TERM)

    # Store processed data for reuse
    processed_data = {}

    if args.summary:
        summary_analysis = SummaryAnalysis()
        summary_analysis.load_entries(matching_entries)
        summary_analysis.analyze()
        summary_analysis.report()

        # Store table data for correlation analysis
        processed_data['summary_data'] = summary_analysis.table_data

    if args.duration:
        hedge_group: LogEntry = get_hedge_group(matching_entries, int(args.duration))
        duration_analysis = DurationAnalysis()
        duration_analysis.load_entry(hedge_group)
        duration_analysis.analyze()
        duration_analysis.report()

        # Store events for this specific hedge group
        hg_id = extract_hedge_group_id(hedge_group)
        processed_data['duration_events'] = {hg_id: duration_analysis.events}

    if args.correlation:
        # Create correlation analysis instance
        correlation_analysis = CorrelationAnalysis()

        # If we need to process all hedge groups for correlation
        if 'summary_data' not in processed_data:
            # Process summary data if not already done
            summary_analysis = SummaryAnalysis()
            summary_analysis.load_entries(matching_entries)
            summary_analysis.analyze()
            processed_data['summary_data'] = summary_analysis.table_data

        # Process duration data for each hedge group if not already done
        if 'duration_events' not in processed_data:
            processed_data['duration_events'] = {}

            for entry in matching_entries:
                hg_id = extract_hedge_group_id(entry)

                # Skip if we've already processed this hedge group
                if hg_id in processed_data['duration_events']:
                    continue

                duration_analysis = DurationAnalysis()
                duration_analysis.load_entry(entry)
                duration_analysis.analyze()
                processed_data['duration_events'][hg_id] = duration_analysis.events

        # Load data into correlation analysis
        for hg_id, events in processed_data['duration_events'].items():
            correlation_analysis.load_duration_events(hg_id, events)

            # Find matching summary data
            for summary_item in processed_data['summary_data']:
                if summary_item['id'] == hg_id:
                    correlation_analysis.load_pnl_data(hg_id, summary_item)
                    break

        # Generate correlation reports
        print("\n=== Correlation Analysis for All Trades ===")
        correlation_analysis.report(only_losses=False)

        print("\n=== Correlation Analysis for Loss Trades Only ===")
        correlation_analysis.report(only_losses=True)

    if args.cancel_fill:
        # Create cancel-fill analysis instance
        cancel_fill_analysis = CancelFillAnalysis()

        # If we need to process all hedge groups for cancel-fill analysis
        if 'summary_data' not in processed_data:
            # Process summary data if not already done
            summary_analysis = SummaryAnalysis()
            summary_analysis.load_entries(matching_entries)
            summary_analysis.analyze()
            processed_data['summary_data'] = summary_analysis.table_data

        # Process duration data for each hedge group if not already done
        if 'duration_events' not in processed_data:
            processed_data['duration_events'] = {}

            for entry in matching_entries:
                hg_id = extract_hedge_group_id(entry)

                # Skip if we've already processed this hedge group
                if hg_id in processed_data['duration_events']:
                    continue

                duration_analysis = DurationAnalysis()
                duration_analysis.load_entry(entry)
                duration_analysis.analyze()
                processed_data['duration_events'][hg_id] = duration_analysis.events

        # Load data into cancel-fill analysis
        for hg_id, events in processed_data['duration_events'].items():
            cancel_fill_analysis.load_duration_events(hg_id, events)

            # Find matching summary data
            for summary_item in processed_data['summary_data']:
                if summary_item['id'] == hg_id:
                    cancel_fill_analysis.load_hedge_group_data(hg_id, summary_item)
                    break

        # Generate cancel-fill report
        print("\n=== Cancel-to-Fill Duration Analysis ===")
        cancel_fill_analysis.report()

if __name__ == "__main__":
    main()
