"""page_bundle.py: the known-property catalogue parsed from the sec_touch headers, and its virtual page module."""

import gzip
import json
from pathlib import Path
import re
import shutil
import sys

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "components" / "sec_touch_sniffer"))

from page_bundle import (  # noqa: E402
    CATALOGUE_MODULE,
    HEADER_DIR,
    PAGE_FILE,
    WEB_DIR,
    bundle_page,
    catalogue_module,
    gzipped_page,
    parse_catalogue,
)

PROTOCOL_IDS = "protocol_ids.h"
FAN_LEVEL = "fan_level.h"


@pytest.fixture
def header_dir(tmp_path: Path) -> Path:
    """A copy of the real headers the catalogue is parsed from, for a test to break."""
    copy = tmp_path / "sec_touch"
    copy.mkdir(parents=True)
    shutil.copy(HEADER_DIR / PROTOCOL_IDS, copy / PROTOCOL_IDS)
    shutil.copy(HEADER_DIR / FAN_LEVEL, copy / FAN_LEVEL)
    return copy


def edit(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    assert old in text, f"fixture edit: {old!r} not in {path.name}"
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def write_page(web_dir: Path, script: str) -> None:
    web_dir.mkdir(exist_ok=True)
    (web_dir / PAGE_FILE).write_text(f'<script type="module">\n{script}</script>\n', encoding="utf-8")


# ── header parse ──


def test_parses_the_known_property_ids() -> None:
    catalogue = parse_catalogue()

    assert catalogue["levelIds"] == [173, 174, 175, 176, 177, 178]
    assert catalogue["labelIds"] == [78, 79, 80, 81, 82, 83]
    assert catalogue["summerVentilationId"] == 48
    assert catalogue["dateId"] == 1
    assert catalogue["clockId"] == 2


def test_parses_a_name_for_every_level_including_not_connected() -> None:
    level_names = parse_catalogue()["levelNames"]

    assert sorted(level_names, key=int) == [str(level) for level in [*range(12), 255]]
    assert level_names["0"] == "off"
    assert level_names["1"] == "level 1"
    assert level_names["3"] == "level 3"
    assert level_names["6"] == "level 6"
    # Levels 7-11 are named after the preset that carries them, through the constants of fan_level.h.
    assert level_names["7"] == "Burst"
    assert level_names["8"] == "Automatic Humidity"
    assert level_names["11"] == "Sleep"
    assert level_names["255"] == "not connected"


def test_parses_all_69_label_names_in_order() -> None:
    label_names = parse_catalogue()["labelNames"]

    assert len(label_names) == 69
    assert label_names[0] == "-"
    assert label_names[21] == "Küche"
    assert label_names[24] == "Bad"
    assert label_names[68] == "leer"


def test_parses_the_summer_ventilation_values() -> None:
    assert parse_catalogue()["summerVentilationNames"] == {"0A00": "on", "0800": "off"}


def test_reads_the_headers_from_the_given_directory(header_dir: Path) -> None:
    edit(header_dir / PROTOCOL_IDS, '"Wohnzimmer"', '"Living room"')
    edit(header_dir / FAN_LEVEL, "BURST_LEVEL = 7;", "BURST_LEVEL = 12;")

    catalogue = parse_catalogue(header_dir)

    assert catalogue["labelNames"][7] == "Living room"
    assert catalogue["levelNames"]["12"] == "Burst"
    assert "7" not in catalogue["levelNames"]


@pytest.mark.parametrize(
    ("file", "old", "new", "error"),
    [
        pytest.param(PROTOCOL_IDS, '    "leer"              // 68\n', "", "NAME_MAPPING", id="label count off"),
        pytest.param(PROTOCOL_IDS, "{BURST_LEVEL, BURST_NAME}", "{BURST_LEVEL, BURSTY_NAME}", "BURSTY_NAME",
                     id="unknown name"),
        pytest.param(PROTOCOL_IDS, '{3, "level 3"}', '{MIN_NORMAL_LEVEL + 2, "level 3"}', "LEVEL_NAMES",
                     id="expression entry"),
        pytest.param(PROTOCOL_IDS, "LEVEL_NAMES = {{", "LEVEL_NAMES_RENAMED = {{", "LEVEL_NAMES",
                     id="missing table"),
        pytest.param(PROTOCOL_IDS, "std::array<LevelName, 13>", "std::array<LevelName, 14>", "LEVEL_NAMES",
                     id="table size off"),
        pytest.param(PROTOCOL_IDS, '"Bad",', '"B\\"ad",', "escape", id="escaped string"),
        pytest.param(PROTOCOL_IDS, "FAN_LEVEL_IDS = {173,", "FAN_LEVEL_IDS = {0xAD,", "FAN_LEVEL_IDS",
                     id="unparseable id"),
        pytest.param(PROTOCOL_IDS, "SUMMER_VENTILATION_PROPERTY_ID = 48;", "SUMMER_VENTILATION_PROPERTY_ID = 6 * 8;",
                     "SUMMER_VENTILATION_PROPERTY_ID", id="expression constant"),
        pytest.param(FAN_LEVEL, "constexpr int SLEEP_LEVEL = 11;", "", "SLEEP_LEVEL", id="missing level constant"),
    ],
)
def test_fails_loudly_on_a_header_it_cannot_parse(header_dir: Path, file: str, old: str, new: str, error: str) -> None:
    edit(header_dir / file, old, new)

    with pytest.raises(ValueError, match=re.escape(error)):
        parse_catalogue(header_dir)


def test_fails_on_a_name_defined_twice_across_two_kinds(header_dir: Path) -> None:
    edit(header_dir / PROTOCOL_IDS, "// Names a fan's label property selects",
         "constexpr const std::array<int, 1> LEVEL_NAMES = {1};\n// Names a fan's label property selects")

    with pytest.raises(ValueError, match="LEVEL_NAMES is defined twice"):
        parse_catalogue(header_dir)


def test_a_header_it_cannot_parse_fails_the_page_build(header_dir: Path) -> None:
    edit(header_dir / PROTOCOL_IDS, "{BURST_LEVEL, BURST_NAME}", "{BURST_LEVEL, BURSTY_NAME}")

    with pytest.raises(ValueError, match="BURSTY_NAME"):
        bundle_page(header_dir=header_dir)


# ── virtual catalogue module ──


def test_the_catalogue_module_exports_the_parsed_catalogue_as_one_const() -> None:
    module = catalogue_module()

    prefix = "export const CATALOGUE = "
    assert module.startswith(prefix)
    assert json.loads(module.removeprefix(prefix).rstrip().removesuffix(";")) == parse_catalogue()


def test_the_bundled_page_inlines_the_catalogue() -> None:
    page = bundle_page()

    assert f"// ── {CATALOGUE_MODULE} ──\nconst CATALOGUE = " in page
    assert json.dumps(parse_catalogue()["labelNames"], ensure_ascii=False) in page


def test_the_catalogue_is_served_from_memory_not_from_web() -> None:
    assert not (WEB_DIR / CATALOGUE_MODULE).exists()


def test_the_page_imports_the_catalogue_built_from_the_given_headers(tmp_path: Path, header_dir: Path) -> None:
    web_dir = tmp_path / "web"
    write_page(web_dir, f'import {{ CATALOGUE }} from "./{CATALOGUE_MODULE}";\nconsole.log(CATALOGUE);\n')
    edit(header_dir / PROTOCOL_IDS, '"Wohnzimmer"', '"Living room"')

    page = bundle_page(web_dir, header_dir)

    assert '"Living room"' in page
    assert "import" not in page
    assert not (web_dir / CATALOGUE_MODULE).exists()


def test_only_the_page_may_import_the_catalogue(tmp_path: Path) -> None:
    web_dir = tmp_path / "web"
    write_page(web_dir, 'import { helper } from "./helper.mjs";\nhelper();\n')
    (web_dir / "helper.mjs").write_text(
        f'import {{ CATALOGUE }} from "./{CATALOGUE_MODULE}";\nexport function helper() {{ return CATALOGUE; }}\n',
        encoding="utf-8",
    )

    with pytest.raises(ValueError, match=f"helper.mjs.*{re.escape(CATALOGUE_MODULE)}"):
        bundle_page(web_dir)


def test_the_gzipped_page_holds_the_catalogue() -> None:
    assert b"const CATALOGUE = " in gzip.decompress(gzipped_page())
