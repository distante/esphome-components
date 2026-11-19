"""Shared helpers for the config tests: find fixtures, run the vendored ESPHome."""

from pathlib import Path
import subprocess
import sys
from typing import Literal

import pytest

FIXTURES_DIR = Path(__file__).parent

Command = Literal["config", "clean", "compile"]


def fixtures(kind: str) -> list[Path]:
    """Every YAML fixture in `valid/` or `invalid/`, sorted."""
    return sorted((FIXTURES_DIR / kind).glob("*.yaml"))


def each_fixture(kind: str) -> pytest.MarkDecorator:
    """Parametrize a test's `fixture` argument over `fixtures(kind)`, one id per file."""
    return pytest.mark.parametrize("fixture", fixtures(kind), ids=lambda p: p.stem)


def run_esphome(command: Command, fixture: Path) -> subprocess.CompletedProcess[str]:
    """Run `esphome <command> <fixture>` and capture its output."""
    return subprocess.run(
        [sys.executable, "-m", "esphome", command, str(fixture)],
        capture_output=True,
        text=True,
        check=False,
    )
