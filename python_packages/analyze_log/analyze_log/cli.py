# analyze_log/analyze_log/cli.py
import argparse
from pathlib import Path

from .analyzers.book import BookAnalyzer, BookDataCollector, BookReporter
from .analyzers.failure import FailureAnalyzer, FailureDataCollector, FailureReporter
from .analyzers.severity import (
    SeverityAnalyzer,
    SeverityDataCollector,
    SeverityReporter,
)
from .core import LogPreprocessor, LogReader


def parse_args():
    parser = argparse.ArgumentParser(description="Trading log analysis tools")
    parser.add_argument("log_path", type=Path, help="Path to the log file")
    parser.add_argument("--failure", action="store_true", help="Run failure analysis")
    parser.add_argument(
        "--book", action="store_true", help="Run order book delay analysis"
    )
    parser.add_argument(
        "--severity", action="store_true", help="Run error/warning severity analysis"
    )
    parser.add_argument(
        "--skip-removing-pre-trading-entries",
        action="store_true",
        help="Skip the removal of pre-trading entries from analysis",
    )
    return parser.parse_args()


def analyze_failures(log_path: Path, skip_removing_pre_trading: bool = False) -> None:
    """Run failure analysis on log file"""
    entries = LogReader.read_log(log_path)
    if not skip_removing_pre_trading:
        entries = LogPreprocessor.remove_pre_trading_entries(entries)

    collector = FailureDataCollector()
    for entry in entries:
        collector.process_entry(entry)

    analyzer = FailureAnalyzer()
    result = analyzer.analyze(collector.stats)

    reporter = FailureReporter()
    reporter.generate_report(result)


def analyze_book(log_path: Path, skip_removing_pre_trading: bool = False) -> None:
    """Run book analysis on log file"""
    entries = LogReader.read_log(log_path)
    if not skip_removing_pre_trading:
        entries = LogPreprocessor.remove_pre_trading_entries(entries)

    collector = BookDataCollector()
    for entry in entries:
        collector.process_entry(entry)

    analyzer = BookAnalyzer()
    result = analyzer.analyze(collector.stats)

    reporter = BookReporter()
    reporter.generate_report(result)


def analyze_severity(log_path: Path, skip_removing_pre_trading: bool = False) -> None:
    """Run severity analysis on log file"""
    entries = LogReader.read_log(log_path)
    if not skip_removing_pre_trading:
        entries = LogPreprocessor.remove_pre_trading_entries(entries)

    collector = SeverityDataCollector()
    for entry in entries:
        collector.process_entry(entry)

    analyzer = SeverityAnalyzer()
    result = analyzer.analyze(collector.stats)

    reporter = SeverityReporter()
    reporter.generate_report(result)


def main():
    args = parse_args()

    if not args.log_path.exists():
        print(f"Error: File {args.log_path} does not exist")
        return

    if not (args.failure or args.book or args.severity):
        print(
            "Error: Please specify at least one analysis type (--failure, --book, or --severity)"
        )
        return

    if args.failure:
        analyze_failures(args.log_path, args.skip_removing_pre_trading_entries)

    if args.book:
        analyze_book(args.log_path, args.skip_removing_pre_trading_entries)

    if args.severity:
        analyze_severity(args.log_path, args.skip_removing_pre_trading_entries)


if __name__ == "__main__":
    main()
