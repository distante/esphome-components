"""Builds our components with real ESPHome core for the host platform and runs their GoogleTest suites.

Reuses ESPHome's own C++ unit test runner (script/cpp_unit_test.py); the only additions are
making our components importable and pointing the runner at tests/host instead of ESPHome's tests.

Usage: .venv/bin/python tests/host/run.py [component ...]   (default: every folder in tests/host)
"""

from functools import partial
import os
from pathlib import Path
import sys

import esphome

ESPHOME_ROOT = Path(esphome.__file__).resolve().parents[1]
REPO_ROOT = Path(__file__).resolve().parents[2]
TESTS_DIR = Path(__file__).resolve().parent

sys.path.insert(0, str(ESPHOME_ROOT / "script"))

from build_helpers import load_test_manifest_overrides  # noqa: E402
from cpp_unit_test import BUILD_FLAGS, PLATFORMIO_GOOGLE_TEST_LIB  # noqa: E402
from build_helpers import build_and_run  # noqa: E402

from esphome import loader  # noqa: E402

# Makes esphome.components.sec_touch resolve to our repo, like external_components does.
loader.install_meta_finder(REPO_ROOT / "components")


def main() -> int:
    components = sys.argv[1:] or sorted(
        p.name for p in TESTS_DIR.iterdir() if p.is_dir() and not p.name.startswith((".", "_"))
    )
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
