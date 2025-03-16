import os
import time
from datetime import datetime, timedelta
from pathlib import Path
from typing import Optional

from rich.panel import Panel
from rich.text import Text
from rich.table import Table

from watch_trading.config.panel_colors import PANEL_COLORS

COLOR = PANEL_COLORS["alive"]


class ProcessAliveMonitor:
    """Monitor trading process status"""

    def __init__(self, project_root: Path, polling_interval: float = 1.0):
        self.project_root = Path(project_root)
        self.polling_interval = polling_interval
        self.pid_dir = self.project_root / "var" / "pid"
        self.last_alive_time: Optional[datetime] = None

    def get_pid_file(self, config_name: str) -> Path:
        """Get PID file path for given config"""
        config_name = config_name.replace("strat", "infra")
        return self.pid_dir / f"{config_name}.pid"

    def is_running(self, config_name: str) -> bool:
        """Check if trading process is running

        Args:
            config_name: Name of the config file (without .json extension)

        Returns:
            True if process is running, False otherwise
        """
        pid_file = self.get_pid_file(config_name)

        if not pid_file.exists():
            if self.last_alive_time is None:
                self.last_alive_time = datetime.now()
            return False

        try:
            # Read PID from file
            pid = int(pid_file.read_text().strip())

            # Try to send signal 0 to process - this only checks if process exists
            # without actually sending a signal
            os.kill(pid, 0)

            # Process is running
            self.last_alive_time = None
            return True

        except (ProcessLookupError, ValueError, PermissionError):
            # Process not running or PID file invalid
            if self.last_alive_time is None:
                self.last_alive_time = datetime.now()
            pid_file.unlink(missing_ok=True)  # Clean up stale pid file
            return False

    def render(self, config_name: str) -> Panel:
        """Render process status panel

        Args:
            config_name: Name of the config file (without .json extension)

        Returns:
            Rich Panel showing process status
        """
        is_alive = self.is_running(config_name)

        table = Table(show_header=False, box=None, collapse_padding=True)
        table.add_column("Property", style=f"bold {COLOR}")
        table.add_column("Value")

        # Add status row
        status = Text("RUNNING", style="bold green") if is_alive else Text("STOPPED", style="bold red")
        table.add_row("Status", status)

        # Add polling information
        table.add_row("Polling Period", f"{self.polling_interval:.1f}s")

        # Add timing information if process is not running
        if not is_alive and self.last_alive_time:
            downtime = datetime.now() - self.last_alive_time
            table.add_row(
                "Down since",
                self.last_alive_time.strftime('%Y-%m-%d %H:%M:%S')
            )
            table.add_row(
                "Downtime",
                str(downtime).split('.')[0]  # Remove microseconds
            )
        elif not is_alive:
            table.add_row("Status", "Never started")

        return Panel(
            table,
            title="Process Monitor",
            border_style=COLOR
        )
