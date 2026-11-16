"""Builds our components with the real ESPHome core for the host platform and runs their GoogleTest suites.

Reuses ESPHome's own C++ unit test runner (script/cpp_unit_test.py, script/build_helpers.py). Those are
ESPHome's internal scripts, not a public API, so this needs ESPHome installed from a source checkout
(`pip install -e ./esphome`, as requirements_test.txt does) and may need updating when ESPHome is bumped.

Usage: python tests/host/run.py [component ...]   (default: every component folder in tests/host)
"""

import argparse
from functools import partial
import os
from pathlib import Path
import sys

TESTS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TESTS_DIR.parents[1]

# Our repo has its own `tests/` and an `esphome/` submodule folder. Keep both off sys.path so that
# `esphome` is the installed package and `tests` is ESPHome's tests package (tests.testing_helpers).
_SHADOWING = {REPO_ROOT, TESTS_DIR}
sys.path[:] = [p for p in sys.path if Path(p or os.getcwd()).resolve() not in _SHADOWING]

import esphome  # noqa: E402

ESPHOME_ROOT = Path(esphome.__file__).resolve().parents[1]


def _fail(message: str) -> None:
    print(f"tests/host: {message}", file=sys.stderr)
    sys.exit(2)


if not (ESPHOME_ROOT / "script" / "build_helpers.py").is_file():
    _fail(f"ESPHome at {ESPHOME_ROOT} has no script/; install it from source: pip install -e ./esphome")

sys.path.insert(0, str(ESPHOME_ROOT / "script"))
try:
    from build_helpers import build_and_run, load_test_manifest_overrides  # noqa: E402
    from cpp_unit_test import BUILD_FLAGS, PLATFORMIO_GOOGLE_TEST_LIB  # noqa: E402
    import tests.testing_helpers  # noqa: E402
except ImportError as e:
    _fail(f"ESPHome's C++ unit test runner API changed ({e}); update tests/host/run.py for this ESPHome version")

if not Path(tests.testing_helpers.__file__).resolve().is_relative_to(ESPHOME_ROOT):
    _fail(f"`tests` resolved to {tests.testing_helpers.__file__}, not ESPHome's tests package")

from esphome import loader  # noqa: E402

# Makes esphome.components.<name> resolve to our repo for our components, like external_components does.
loader.install_meta_finder(REPO_ROOT / "components")


def main() -> int:
    available = sorted(p.name for p in TESTS_DIR.iterdir() if p.is_dir() and not p.name.startswith((".", "_")))
    parser = argparse.ArgumentParser(description="Run the host-platform component tests in tests/host.")
    parser.add_argument("components", nargs="*", help=f"components to test (default: all of {', '.join(available)})")
    components = parser.parse_args().components or available
    if unknown := sorted(set(components) - set(available)):
        parser.error(f"no tests/host folder for: {', '.join(unknown)}")

    os.environ["ASAN_OPTIONS"] = "detect_leaks=0:allocator_may_return_null=1"
    return build_and_run(
        selected_components=components,
        tests_dir=TESTS_DIR,
        manifest_override_loader=partial(load_test_manifest_overrides, tests_dir=TESTS_DIR),
        config_prefix="hosttests",
        friendly_name="Host Unit Tests",
        libraries=PLATFORMIO_GOOGLE_TEST_LIB,
        build_flags=BUILD_FLAGS,
        main_entry=str(ESPHOME_ROOT / "tests" / "components" / "main.cpp"),
        label="host tests",
    )


if __name__ == "__main__":
    sys.exit(main())
