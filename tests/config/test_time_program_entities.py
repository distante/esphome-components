"""AC 4 of .specs/time-programs.md: every time-program entity carries its entity category and the pair's device_id.

Reads the config `esphome config` prints for one valid fixture, so the check covers what codegen gets.
"""

from pathlib import Path

from esphome import loader
from esphome.const import (
    CONF_DEVICE_ID,
    CONF_ENTITY_CATEGORY,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
import yaml

from esphome_run import FIXTURES_DIR, run_esphome

REPO = Path(__file__).resolve().parents[2]
# Makes esphome.components.sec_touch resolve to this repo, like external_components does.
loader.install_meta_finder(REPO / "components")

from esphome.components import sec_touch  # noqa: E402

FIXTURE = FIXTURES_DIR / "valid" / "sec_touch_time_programs_entities.yaml"
FIELD_KINDS = (sec_touch.CONF_TIME, sec_touch.CONF_LEVEL, sec_touch.CONF_ENABLED)
# A live-state entity has no category: it shows the pair's state.
NO_CATEGORY = None


def validated_entry() -> dict:
    """The fixture's one time_programs entry, as `esphome config` prints it."""
    result = run_esphome("config", FIXTURE)
    assert result.returncode == 0, result.stdout + result.stderr
    (entry,) = yaml.safe_load(result.stdout)["sec_touch"][sec_touch.CONF_TIME_PROGRAMS]
    return entry


def test_every_entity_has_its_category_and_the_pairs_device() -> None:
    entry = validated_entry()
    device_id = entry[CONF_DEVICE_ID]
    entities = {key: entry[key] for key in sec_touch.OPTIONAL_ENTITY_KEYS}
    expected = {
        sec_touch.CONF_REFRESH: ENTITY_CATEGORY_CONFIG,
        sec_touch.CONF_WRITE_FAILED: ENTITY_CATEGORY_DIAGNOSTIC,
        **{key: NO_CATEGORY for key in sec_touch.LIVE_STATE_KEYS},
    }
    for fields in entry[sec_touch.CONF_FIELD_ENTITIES]:
        for kind in FIELD_KINDS:
            name = f"{sec_touch.CONF_SWITCH_TIME} {fields[sec_touch.CONF_SWITCH_TIME]} {kind}"
            entities[name] = fields[kind]
            expected[name] = ENTITY_CATEGORY_CONFIG

    assert {name: entity.get(CONF_ENTITY_CATEGORY) for name, entity in entities.items()} == expected
    assert {name: entity.get(CONF_DEVICE_ID) for name, entity in entities.items()} == {
        name: device_id for name in expected
    }
