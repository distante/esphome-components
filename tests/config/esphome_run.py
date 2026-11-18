"""Shared helpers for the config tests: find fixtures, run the vendored ESPHome."""

from pathlib import Path
import subprocess
import sys

FIXTURES_DIR = Path(__file__).parent


def fixtures(kind: str) -> list[Path]:
    """Every YAML fixture in `valid/` or `invalid/`, sorted."""
    return sorted((FIXTURES_DIR / kind).glob("*.yaml"))


def esphome(command: str, fixture: Path) -> subprocess.CompletedProcess[str]:
    """Run `esphome <command> <fixture>` and capture its output."""
    return subprocess.run(
        [sys.executable, "-m", "esphome", command, str(fixture)],
        capture_output=True,
        text=True,
        check=False,
    )
