# analyze_log/analyze_log/analyzers/severity/reporter.py
from typing import Dict, Any
from rich.console import Console
from rich.theme import Theme
from rich.panel import Panel
from rich.box import ROUNDED

class SeverityReporter:
    def __init__(self):
        self.console = Console(
            theme=Theme({
                'component': 'cyan',
                'error_count': 'red',
                'warn_count': 'yellow',
                'timestamp': 'bright_black',
                'error_level': 'red',
                'warn_level': 'yellow',
                'title': 'magenta'
            }),
            force_terminal=True,
            width=32767
        )

    def generate_report(self, analysis_result: Dict[str, Any]) -> None:
        """Generate a formatted report of severity analysis"""
        title_panel = Panel(
            "Severity Analysis",
            box=ROUNDED,
            style="title",
            padding=(0, 1),
            expand=False
        )

        self.console.print(title_panel)
        self.console.print("")

        # Print summary
        self.console.print("Summary:", style="bold")
        self.console.print("")

        for component in sorted(analysis_result['summary'].keys()):
            stats = analysis_result['summary'][component]

            self.console.print(
                f"{component}",
                style="component"
            )
            self.console.print(
                f"  Errors: {stats['errors']}",
                style="error_count"
            )
            self.console.print(
                f"  Warnings: {stats['warnings']}",
                style="warn_count"
            )
            self.console.print("")

        # Print detailed logs
        self.console.print("Details:", style="bold")
        self.console.print("")

        for component in sorted(analysis_result['logs'].keys()):
            self.console.print(
                f"{component}",
                style="component"
            )

            component_logs = analysis_result['logs'][component]

            # Print errors
            if component_logs['errors']:
                for log in component_logs['errors']:
                    self.console.print(
                        f"  {log.timestamp.strftime('%Y-%m-%d %H:%M:%S.%f')} | ",
                        style="timestamp",
                        end=""
                    )
                    self.console.print(
                        "ERRO",
                        style="error_level",
                        end=""
                    )
                    self.console.print(f" | {log.content}")

            # Print warnings
            if component_logs['warnings']:
                for log in component_logs['warnings']:
                    self.console.print(
                        f"  {log.timestamp.strftime('%Y-%m-%d %H:%M:%S.%f')} | ",
                        style="timestamp",
                        end=""
                    )
                    self.console.print(
                        "WARN",
                        style="warn_level",
                        end=""
                    )
                    self.console.print(f" | {log.content}")

            self.console.print("")
