"""Run every YAML fixture through `esphome config` with the vendored ESPHome.

- `valid/*.yaml` must pass validation.
- `invalid/*.yaml` must fail, and the output must contain the text from the
  fixture's `# expect-error: <text>` line.
"""

from pathlib import Path
import subprocess
import sys

import pytest

FIXTURES_DIR = Path(__file__).parent
EXPECT_ERROR_PREFIX = "# expect-error:"


def fixtures(kind: str) -> list[Path]:
    return sorted((FIXTURES_DIR / kind).glob("*.yaml"))


def esphome_config(fixture: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-m", "esphome", "config", str(fixture)],
        capture_output=True,
        text=True,
        check=False,
    )


def expected_error(fixture: Path) -> str:
    for line in fixture.read_text(encoding="utf-8").splitlines():
        if line.startswith(EXPECT_ERROR_PREFIX):
            return line.removeprefix(EXPECT_ERROR_PREFIX).strip()
    pytest.fail(f"{fixture.name} has no '{EXPECT_ERROR_PREFIX} <text>' line")


@pytest.mark.parametrize("fixture", fixtures("valid"), ids=lambda p: p.stem)
def test_valid_config_passes(fixture: Path) -> None:
    result = esphome_config(fixture)

    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("fixture", fixtures("invalid"), ids=lambda p: p.stem)
def test_invalid_config_fails_with_expected_error(fixture: Path) -> None:
    error = expected_error(fixture)

    result = esphome_config(fixture)

    output = result.stdout + result.stderr
    assert result.returncode != 0, output
    assert error in output, output
