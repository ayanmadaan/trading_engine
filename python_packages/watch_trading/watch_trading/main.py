import argparse
from pathlib import Path
from watch_trading.core import TradingMonitor

REFRESH_INTERVAL = 1.0

def parse_arguments():
    parser = argparse.ArgumentParser(description='Trading Monitor')
    parser.add_argument('status_path',
                       type=Path,
                       help='Path to the status file')
    parser.add_argument('-i', '--interval',
                       type=float,
                       default=1.0,
                       help='Refresh interval in seconds (default: 1.0)')
    args = parser.parse_args()

    if not args.status_path.exists():
        raise FileNotFoundError(f"Status file not found: {args.status_path}")

    if args.interval <= 0:
        parser.error("Refresh interval must be positive")

    return args

def run_monitor(status_path: Path, refresh_interval: float):
    monitor = TradingMonitor(status_path, refresh_interval)
    monitor.run()

def main():
    try:
        args: argparse.Namespace = parse_arguments()
        run_monitor(args.status_path, args.interval)
    except KeyboardInterrupt:
        print("Monitor stopped by user")
    except Exception as e:
        print(f"Error: {e}")
        exit(1)

if __name__ == '__main__':
    main()
