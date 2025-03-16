# analyze_log/analyze_log/analyzers/failure/reporter.py
from typing import Dict, Tuple

from rich.box import ROUNDED
from rich.console import Console
from rich.panel import Panel
from rich.theme import Theme


class FailureReporter:
    def __init__(self):
        self.console = Console(
            theme=Theme(
                {
                    "action": "cyan",
                    "reason": "yellow",
                    "count": "white",
                    "percentage": "bright_cyan",
                    "title": "magenta",
                }
            ),
            force_terminal=True,  # Add this line
            width=32767,  # Add this line
        )

    def generate_report(
        self, analysis_result: Dict[str, Dict[str, Tuple[int, int]]]
    ) -> None:
        """Generate a formatted report of failure analysis"""
        title_panel = Panel(
            "Failure Analysis", box=ROUNDED, style="title", padding=(0, 1), expand=False
        )

        self.console.print(title_panel)
        self.console.print("")  # Add space after title

        for action, reasons in sorted(analysis_result.items()):
            total = sum(count for count, _ in reasons.values())

            # Print action with total count
            self.console.print(f"{action} ", style="action", end="")
            self.console.print(f"({total})", style="count")

            # Print reasons with counts and percentages
            for reason, (count, percentage) in sorted(reasons.items()):
                self.console.print("  - ", style="white", end="")
                self.console.print(f"{reason}: ", style="reason", end="")
                self.console.print(str(count), style="count", end="")
                self.console.print(f" ({percentage}%)", style="percentage")

            # Add newline between actions
            self.console.print("")
