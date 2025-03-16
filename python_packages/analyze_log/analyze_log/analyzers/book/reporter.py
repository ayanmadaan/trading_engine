# analyze_log/analyze_log/analyzers/book/reporter.py
from typing import Any, Dict

from rich.box import ROUNDED
from rich.console import Console
from rich.panel import Panel
from rich.theme import Theme


class BookReporter:
    def __init__(self):
        self.console = Console(
            theme=Theme(
                {
                    "exchange": "cyan",
                    "label": "yellow",
                    "number": "white",
                    "title": "magenta",
                }
            ),
            force_terminal=True,  # Add this line
            width=32767,  # Add this line
        )

    def generate_report(self, analysis_result: Dict[str, Dict[str, Any]]) -> None:
        """Generate a formatted report of book delay analysis"""
        title_panel = Panel(
            "Book Delay Analysis",
            box=ROUNDED,
            style="title",
            padding=(0, 1),
            expand=False,
        )

        self.console.print(title_panel)
        self.console.print("")  # Add space after title

        for exchange, stats in sorted(analysis_result.items()):
            # Print exchange header
            self.console.print(f"{exchange}", style="exchange")

            # Print statistics
            self.console.print("  Samples: ", style="label", end="")
            self.console.print(f"{stats['count']}", style="number")

            self.console.print("  Average Delay: ", style="label", end="")
            self.console.print(f"{stats['avg_delay']:.2f}ms", style="number")

            self.console.print("  Median Delay: ", style="label", end="")
            self.console.print(f"{stats['median_delay']:.2f}ms", style="number")

            self.console.print("  Min Delay: ", style="label", end="")
            self.console.print(f"{stats['min_delay']:.2f}ms", style="number")

            self.console.print("  Max Delay: ", style="label", end="")
            self.console.print(f"{stats['max_delay']:.2f}ms", style="number")

            # Add newline between exchanges
            self.console.print("")
