"""Compile every valid YAML fixture with `esphome compile` (opt-in: `--compile`).

`esphome config` never builds, so it can't see a header that includes a
component ESPHome didn't add to the build (missing `AUTO_LOAD`). This can.
It also fails on any compiler warning in this repo's components, such as `-Wformat`.
"""

from pathlib import Path

import pytest

from esphome_run import each_fixture, run_esphome

OUTPUT_TAIL_LINES = 60

REPO_COMPONENTS_DIR = Path(__file__).parents[2] / "components"
# Matched against the copies in the build tree: `.../src/esphome/components/<name>/...`.
REPO_COMPONENT_PATHS = tuple(
    f"components/{d.name}/" for d in REPO_COMPONENTS_DIR.iterdir() if d.is_dir()
)

# Without a logger, every ESP_LOG* call is compiled out and -Wformat never fires.
LOGGER_LEVEL_LINE = "level: VERY_VERBOSE"


def repo_component_warnings(output: list[str]) -> list[str]:
    """Compiler warnings from this repo's components, not ESPHome core or libraries."""
    return [
        line
        for line in output
        if "warning:" in line and any(path in line for path in REPO_COMPONENT_PATHS)
    ]


def build_dir(fixture: Path) -> Path:
    """Where `esphome compile` builds `fixture`, named after its `esphome: name:`."""
    for line in fixture.read_text().splitlines():
        if line.strip().startswith("name:"):
            name = line.split(":", 1)[1].strip()
            return fixture.parent / ".esphome" / "build" / name
    raise AssertionError(f"{fixture.name} has no `name:`")


@pytest.mark.compile
@each_fixture("valid")
def test_valid_config_compiles(fixture: Path) -> None:
    assert LOGGER_LEVEL_LINE in fixture.read_text(), (
        f"{fixture.name} needs `logger: {LOGGER_LEVEL_LINE}` so its log calls are compiled"
    )

    # An incremental build skips unchanged files and so prints no warnings for them.
    cleaned = run_esphome("clean", fixture)
    assert cleaned.returncode == 0, cleaned.stdout + cleaned.stderr
    assert not build_dir(fixture).exists(), "esphome clean left the build dir (ESPHOME_SKIP_CLEAN_BUILD?)"

    result = run_esphome("compile", fixture)

    output = (result.stdout + result.stderr).splitlines()
    # The compiler error sits far above the end of a ninja log; show it first.
    errors = [line for line in output if "error:" in line]
    report = errors + ["--- last lines ---"] + output[-OUTPUT_TAIL_LINES:]
    assert result.returncode == 0, "\n".join(report)
    warnings = repo_component_warnings(output)
    assert warnings == [], "\n".join(warnings)
