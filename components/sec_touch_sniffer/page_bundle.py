"""Bundles the sniffer page into one gzipped HTML file for flash.

The page (web/sniffer_page.html) imports plain ES modules from web/ so `node --test` can test them. The device
serves a single file, so the build inlines those modules into the page's module script: dependencies first, each
once, with their `import` lines and `export` keywords removed. Only the forms the page uses are supported, and
anything else fails the build rather than shipping a page that breaks in the browser:

- `import { a, b } from "./name.mjs";` (may span lines; no `as` renames)
- `export function`, `export async function`, `export const`, `export let`, `export class`

`catalogue.mjs` is virtual: no file in web/. The resolver serves it from memory, built at every bundle from six C++
headers: the known-property catalogue (.specs/sniffer.md) from sec_touch's protocol_ids.h and fan_level.h, the units a
time of day is checked against from sec_touch's time_units.h, the write probe's value rules from sec_touch's
data_message.h, and the default discover range, the goal names and the cap on a scan's ID list from this component's
property_slots.h and scan_goal.h. Only the page may import it; the tested modules
take the catalogue as an argument.

sec_touch's header_reader.py reads the headers; its docstring lists the declaration forms it can read. Anything the
catalogue uses that it cannot read fails the build.
"""

import gzip
import importlib.util
import json
from pathlib import Path
import re

WEB_DIR = Path(__file__).parent / "web"
PAGE_FILE = "sniffer_page.html"
HEADER_DIR = Path(__file__).parent.parent / "sec_touch"
SNIFFER_HEADER_DIR = Path(__file__).parent
CATALOGUE_MODULE = "catalogue.mjs"

_SCRIPT = re.compile(r'(<script type="module">)(.*?)(</script>)', re.S)
_IMPORT = re.compile(r'^import\s*\{([^}]*)\}\s*from\s*"\./([\w.-]+\.mjs)";[ \t]*\n?', re.M)
_EXPORT = re.compile(r"^export (?=(?:async )?function |const |let |class )", re.M)
_LEFTOVER = re.compile(r"^[ \t]*(?:import|export)\b", re.M)
_TOP_LEVEL_NAME = re.compile(r"^(?:async )?(?:function\*?|const|let|class) (\w+)", re.M)

# The six parsed headers.
PROTOCOL_IDS_FILE = "protocol_ids.h"
FAN_LEVEL_FILE = "fan_level.h"
PROPERTY_SLOTS_FILE = "property_slots.h"
SCAN_GOAL_FILE = "scan_goal.h"
DATA_MESSAGE_FILE = "data_message.h"
TIME_UNITS_FILE = "time_units.h"
# NUL: the SET frames a probe's value with `%s`, so the value ends at its first NUL. The wire bytes come from
# data_message.h.
NUL_BYTE = 0


def _load_header_reader():
    """sec_touch's header_reader module, loaded by path: this file runs both as part of the ESPHome component and on
    its own (tests/python, the page preview in the readme), where `esphome.components.sec_touch` doesn't resolve."""
    spec = importlib.util.spec_from_file_location("sec_touch_header_reader", HEADER_DIR / "header_reader.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


HeaderReader = _load_header_reader().HeaderReader


def _module_body(source: str, name: str, resolve) -> str:
    """The source with its imports resolved through `resolve` and removed, and `export` dropped."""
    for names, dependency in _IMPORT.findall(source):
        if " as " in names:
            raise ValueError(f"{name}: `as` renames are not supported by the page bundle")
        if dependency == CATALOGUE_MODULE and name != PAGE_FILE:
            raise ValueError(
                f"{name}: only {PAGE_FILE} may import {CATALOGUE_MODULE}; take the catalogue as an argument instead"
            )
        resolve(dependency)
    body = _EXPORT.sub("", _IMPORT.sub("", source))
    leftover = _LEFTOVER.search(body)
    if leftover is not None:
        line = body[leftover.start() :].splitlines()[0]
        raise ValueError(f"{name}: unsupported import or export form for the page bundle: {line.strip()}")
    return body


def _names_by(header: HeaderReader, name: str, key_type: type) -> dict:
    """{key: name} from the header's table `name` of {key, name} pairs, failing on a wrong type or a repeated key."""
    entries = header.get(header.pair_tables, name)
    where = f"{header.defined_in[name]}: {name}"
    names = {}
    for key, text in entries:
        if not isinstance(key, key_type) or not isinstance(text, str):
            raise ValueError(f"{where}: entry {{{key}, {text}}} has the wrong types")
        if str(key) in names:
            raise ValueError(f"{where}: {key} appears twice")
        names[str(key)] = text
    return names


def parse_catalogue(header_dir: Path = HEADER_DIR, sniffer_header_dir: Path = SNIFFER_HEADER_DIR) -> dict:
    """The contents of catalogue.mjs, read from the six headers the module docstring names."""
    header = HeaderReader()
    header.read(header_dir / FAN_LEVEL_FILE)
    header.read(header_dir / PROTOCOL_IDS_FILE)
    header.read(header_dir / DATA_MESSAGE_FILE)
    header.read(header_dir / TIME_UNITS_FILE)
    header.read(sniffer_header_dir / PROPERTY_SLOTS_FILE)
    header.read(sniffer_header_dir / SCAN_GOAL_FILE)
    return {
        "levelIds": header.get(header.int_arrays, "FAN_LEVEL_IDS"),
        "labelIds": header.get(header.int_arrays, "FAN_LABEL_IDS"),
        "summerVentilationId": header.get(header.ints, "SUMMER_VENTILATION_PROPERTY_ID"),
        "dateId": header.get(header.ints, "DATE_PROPERTY_ID"),
        "clockId": header.get(header.ints, "CLOCK_PROPERTY_ID"),
        "timeProgramEnabledIds": header.get(header.int_arrays, "TIME_PROGRAM_ENABLED_IDS"),
        "timeProgramTimeIds": header.get(header.int_arrays, "TIME_PROGRAM_TIME_IDS"),
        "timeProgramLevelIds": header.get(header.int_arrays, "TIME_PROGRAM_LEVEL_IDS"),
        "switchTimeCount": header.get(header.ints, "SWITCH_TIME_COUNT"),
        "maxSwitchTimeLevel": header.get(header.ints, "MAX_SWITCH_TIME_LEVEL"),
        "timeUnits": {
            "hoursPerDay": header.get(header.ints, "HOURS_PER_DAY"),
            "minutesPerHour": header.get(header.ints, "MINUTES_PER_HOUR"),
            "secondsPerMinute": header.get(header.ints, "SECONDS_PER_MINUTE"),
        },
        "enabledMaskNames": {
            "none": header.get(header.strings, "ENABLED_MASK_NONE_NAME"),
            "separator": header.get(header.strings, "ENABLED_MASK_SEPARATOR"),
            "suffix": header.get(header.strings, "ENABLED_MASK_SUFFIX"),
        },
        "levelNames": _names_by(header, "LEVEL_NAMES", int),
        "labelNames": header.get(header.string_tables, "NAME_MAPPING"),
        "summerVentilationNames": _names_by(header, "SUMMER_VENTILATION_NAMES", str),
        "discoverRange": {
            "from": header.get(header.ints, "DEFAULT_DISCOVER_FROM"),
            "to": header.get(header.ints, "DEFAULT_DISCOVER_TO"),
        },
        "goals": {
            "discover": header.get(header.strings, "SCAN_GOAL_DISCOVER_NAME"),
            "check": header.get(header.strings, "SCAN_GOAL_CHECK_NAME"),
        },
        "scanListCap": header.get(header.ints, "MAX_SCAN_LIST_IDS"),
        "probeValueCap": header.get(header.ints, "SET_VALUE_CAP"),
        "probeForbiddenBytes": [
            NUL_BYTE,
            header.get(header.ints, "WIRE_TAB"),
            header.get(header.ints, "WIRE_STX"),
            header.get(header.ints, "WIRE_ETX"),
        ],
    }


def catalogue_module(header_dir: Path = HEADER_DIR) -> str:
    """The source of the virtual catalogue.mjs: one `CATALOGUE` const, built from the headers at every call.

    `<` is written as \\u003c (the same string to JSON and JS) so no name can close the page's inline script with
    `</script` or open an HTML comment with `<!--`.
    """
    catalogue = json.dumps(parse_catalogue(header_dir), ensure_ascii=False).replace("<", "\\u003c")
    return f"export const CATALOGUE = {catalogue};\n"


def bundle_page(web_dir: Path = WEB_DIR, header_dir: Path = HEADER_DIR) -> str:
    """The page with every module it imports inlined into its module script."""
    html = (web_dir / PAGE_FILE).read_text(encoding="utf-8")
    scripts = _SCRIPT.findall(html)
    if len(scripts) != 1:
        raise ValueError(f"{PAGE_FILE} must have exactly one <script type=\"module\">, found {len(scripts)}")

    bodies = []
    done = set()
    in_progress = set()

    def resolve(module: str):
        if module in done:
            return
        if module in in_progress:
            raise ValueError(f"{module}: import cycle in the page modules")
        in_progress.add(module)
        if module == CATALOGUE_MODULE:
            source = catalogue_module(header_dir)
        else:
            source = (web_dir / module).read_text(encoding="utf-8")
        body = _module_body(source, module, resolve)
        in_progress.discard(module)
        done.add(module)
        bodies.append(f"// ── {module} ──\n{body.strip()}\n")

    page_script = _module_body(scripts[0][1], PAGE_FILE, resolve)
    script = "\n".join([*bodies, f"// ── {PAGE_FILE} ──\n{page_script.strip()}\n"])

    names = _TOP_LEVEL_NAME.findall(script)
    duplicates = sorted({n for n in names if names.count(n) > 1})
    if duplicates:
        raise ValueError(f"page modules declare the same top-level names: {', '.join(duplicates)}")

    return _SCRIPT.sub(lambda m: f"{m.group(1)}\n{script}{m.group(3)}", html, count=1)


def gzipped_page(web_dir: Path = WEB_DIR, header_dir: Path = HEADER_DIR) -> bytes:
    """The bundled page, gzipped. mtime=0 keeps the bytes the same for the same page."""
    return gzip.compress(bundle_page(web_dir, header_dir).encode("utf-8"), mtime=0)
