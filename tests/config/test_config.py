"""Run every YAML fixture through `esphome config` with the vendored ESPHome.

- `valid/*.yaml` must pass validation.
- `invalid/*.yaml` must fail, and the output must contain the text of each of
  the fixture's `# expect-error: <text>` lines.
"""

from pathlib import Path

import pytest

from esphome_run import each_fixture, run_esphome

EXPECT_ERROR_PREFIX = "# expect-error:"


def expected_errors(fixture: Path) -> list[str]:
    errors = [
        line.removeprefix(EXPECT_ERROR_PREFIX).strip()
        for line in fixture.read_text(encoding="utf-8").splitlines()
        if line.startswith(EXPECT_ERROR_PREFIX)
    ]
    if not errors:
        pytest.fail(f"{fixture.name} has no '{EXPECT_ERROR_PREFIX} <text>' line")
    return errors


@each_fixture("valid")
def test_valid_config_passes(fixture: Path) -> None:
    result = run_esphome("config", fixture)

    assert result.returncode == 0, result.stdout + result.stderr


@each_fixture("invalid")
def test_invalid_config_fails_with_expected_error(fixture: Path) -> None:
    errors = expected_errors(fixture)

    result = run_esphome("config", fixture)

    output = result.stdout + result.stderr
    assert result.returncode != 0, output
    for error in errors:
        assert error in output, output
