import json
from pathlib import Path
from typing import Any


class JsonReader:
    """Low level JSON file reader"""

    def read(self, path: Path) -> dict[str, Any]:
        """Read and parse JSON file

        Args:
            path: Path to JSON file

        Returns:
            Parsed JSON data as dictionary

        Raises:
            FileNotFoundError: If file doesn't exist
            json.JSONDecodeError: If file contains invalid JSON
            RuntimeError: If unexpected error occurs
        """
        try:
            with open(path) as f:
                return json.load(f)
        except (FileNotFoundError, json.JSONDecodeError):
            raise
        except Exception as e:
            raise RuntimeError(f"Unexpected error reading JSON file: {str(e)}")
