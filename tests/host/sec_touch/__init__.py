"""Host test build overrides for sec_touch, picked up by tests/host/run.py (load_test_manifest_overrides)."""

from esphome.components.logger import request_log_listener
from tests.testing_helpers import ComponentManifestOverride


def override_manifest(manifest: ComponentManifestOverride) -> None:
    # The logger keeps no log callback unless a component asks for one; LogCapture (common.h) is that one.
    async def to_code_testing(config):
        request_log_listener()

    manifest.to_code = to_code_testing
