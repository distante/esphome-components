""".specs/time-programs.md, "Live state": a pair gets one live state when its fan follows the running level or it has a
live-state entity, never two.

Reads the config `esphome config` prints, so the check covers what codegen gets.
"""

from pathlib import Path

from esphome import loader
import yaml

from esphome_run import FIXTURES_DIR, run_esphome

REPO = Path(__file__).resolve().parents[2]
# Makes esphome.components.sec_touch resolve to this repo, like external_components does.
loader.install_meta_finder(REPO / "components")

from esphome.components import sec_touch  # noqa: E402


def live_state_pairs(fixture: str) -> list[int]:
    """The fan_number of each live state the fixture's hub builds, in order."""
    result = run_esphome("config", FIXTURES_DIR / "valid" / fixture)
    assert result.returncode == 0, result.stdout + result.stderr
    hub = yaml.safe_load(result.stdout)["sec_touch"]
    return [entry[sec_touch.CONF_FAN_NUMBER] for entry in hub.get(sec_touch.CONF_LIVE_STATES, [])]


def test_each_following_fan_gets_a_live_state_without_time_programs() -> None:
    assert live_state_pairs("sec_touch_fan_follows_time_program.yaml") == [1, 2]


def test_a_following_fan_and_live_state_entities_share_one_live_state() -> None:
    assert live_state_pairs("sec_touch_time_programs_live_state.yaml") == [1, 2]


def test_no_fan_follows_and_no_live_state_entity_builds_no_live_state() -> None:
    assert live_state_pairs("sec_touch_hub_only.yaml") == []
