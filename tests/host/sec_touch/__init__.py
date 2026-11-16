from tests.testing_helpers import ComponentManifestOverride


def override_manifest(manifest: ComponentManifestOverride) -> None:
    # The hub's headers include button, text_sensor and fan; load them so their sources build.
    manifest.dependencies = [*manifest.dependencies, "button", "text_sensor", "fan"]
