"""AC 4 of .specs/time-programs.md: every time-program entity carries its entity category, the pair's device_id and the
pair's web_server block.

Reads the config `esphome config` prints for one valid fixture, so the check covers what codegen gets.
"""

from pathlib import Path

from esphome import loader
from esphome.const import (
    CONF_DEVICE_ID,
    CONF_ENTITY_CATEGORY,
    CONF_SORTING_GROUP_ID,
    CONF_WEB_SERVER,
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
WEB_SERVER_FIXTURE = FIXTURES_DIR / "valid" / "sec_touch_time_programs_web_server.yaml"
FIELD_KINDS = (sec_touch.CONF_TIME, sec_touch.CONF_LEVEL, sec_touch.CONF_ENABLED)
# A live-state entity has no category: it shows the pair's state.
NO_CATEGORY = None


def validated_entries(fixture: Path) -> list[dict]:
    """The fixture's time_programs entries, as `esphome config` prints them."""
    result = run_esphome("config", fixture)
    assert result.returncode == 0, result.stdout + result.stderr
    return yaml.safe_load(result.stdout)["sec_touch"][sec_touch.CONF_TIME_PROGRAMS]


def validated_entry() -> dict:
    """The entity fixture's one time_programs entry."""
    (entry,) = validated_entries(FIXTURE)
    return entry


def entities_of(entry: dict) -> dict[str, dict]:
    """Every entity of a pair, keyed by its optional-entity key or `switch_time <n> <kind>`."""
    entities = {key: entry[key] for key in sec_touch.OPTIONAL_ENTITY_KEYS if key in entry}
    for fields in entry[sec_touch.CONF_FIELD_ENTITIES]:
        for kind in FIELD_KINDS:
            entities[f"{sec_touch.CONF_SWITCH_TIME} {fields[sec_touch.CONF_SWITCH_TIME]} {kind}"] = fields[kind]
    return entities


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


def test_every_entity_gets_the_pairs_web_server_unless_it_has_its_own() -> None:
    groups = {
        entry[sec_touch.CONF_FAN_NUMBER]: {
            name: entity.get(CONF_WEB_SERVER, {}).get(CONF_SORTING_GROUP_ID)
            for name, entity in entities_of(entry).items()
        }
        for entry in validated_entries(WEB_SERVER_FIXTURE)
    }
    pair_1 = {name: "group_1" for name in groups[1]}
    # The fixture's refresh button sets its own web_server.
    pair_1[sec_touch.CONF_REFRESH] = "group_settings"

    assert len(groups[1]) == len(sec_touch.OPTIONAL_ENTITY_KEYS) + 2 * len(FIELD_KINDS)
    assert groups == {1: pair_1, 2: {name: "group_2" for name in groups[2]}}
