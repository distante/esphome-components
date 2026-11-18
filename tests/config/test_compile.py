"""Compile every valid YAML fixture with `esphome compile` (opt-in: `--compile`).

`esphome config` never builds, so it can't see a header that includes a
component ESPHome didn't add to the build (missing `AUTO_LOAD`). This can.
"""

from pathlib import Path

import pytest

from esphome_run import esphome, fixtures

OUTPUT_TAIL_LINES = 60


@pytest.mark.compile
@pytest.mark.parametrize("fixture", fixtures("valid"), ids=lambda p: p.stem)
def test_valid_config_compiles(fixture: Path) -> None:
    result = esphome("compile", fixture)

    output = (result.stdout + result.stderr).splitlines()
    # The compiler error sits far above the end of a ninja log; show it first.
    errors = [line for line in output if "error:" in line]
    report = errors + ["--- last lines ---"] + output[-OUTPUT_TAIL_LINES:]
    assert result.returncode == 0, "\n".join(report)
