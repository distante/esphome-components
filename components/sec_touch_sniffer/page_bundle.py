"""Bundles the sniffer page into one gzipped HTML file for flash.

The page (web/sniffer_page.html) imports plain ES modules from web/ so `node --test` can test them. The device
serves a single file, so the build inlines those modules into the page's module script: dependencies first, each
once, with their `import` lines and `export` keywords removed. Only the forms the page uses are supported, and
anything else fails the build rather than shipping a page that breaks in the browser:

- `import { a, b } from "./name.mjs";` (may span lines; no `as` renames)
- `export function`, `export async function`, `export const`, `export let`, `export class`

`catalogue.mjs` is virtual: no file in web/. The resolver serves it from memory, built at every bundle from sec_touch's
protocol_ids.h and fan_level.h (the known-property catalogue, .specs/sniffer.md), plus the default discover range
and the goal names from this component's property_slots.h and scan_goal.h. Only the page may import it; the tested
modules take the catalogue as an argument. A header it cannot read fails the build.
"""

import gzip
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

# The catalogue headers. Each regex reads one declaration form; see the comment at the top of protocol_ids.h.
PROTOCOL_IDS_FILE = "protocol_ids.h"
FAN_LEVEL_FILE = "fan_level.h"
# The sniffer headers of the default discover range and the goal names, read with the same regexes and limits.
PROPERTY_SLOTS_FILE = "property_slots.h"
SCAN_GOAL_FILE = "scan_goal.h"
_STRING_OR_COMMENT = re.compile(r'"(?:[^"\\\n]|\\.)*"|//[^\n]*|/\*.*?\*/', re.S)
_STRING = re.compile(r'"(?:[^"\\\n]|\\.)*"')
_INT_CONSTANT = re.compile(r"\bconstexpr\s+(?:const\s+)?(?:int|uint8_t)\s+(\w+)\s*=\s*([^;]*);")
_STRING_CONSTANT = re.compile(r"\bconstexpr\s+const\s+char\s*\*\s*(\w+)\s*=\s*([^;]*);")
_INT_ARRAY = re.compile(r"\bconstexpr\s+(?:const\s+)?std::array<int,\s*(\w+)>\s+(\w+)\s*=\s*\{([^{}]*)\};")
_PAIR_TABLE = re.compile(r"\bconstexpr\s+(?:const\s+)?std::array<\w+,\s*(\w+)>\s+(\w+)\s*=\s*\{\{(.*?)\}\};", re.S)
_PAIR = re.compile(r"\{([^{}]*)\}")
_STRING_TABLE = re.compile(r"\bconstexpr\s+const\s+char\s*\*\s*(\w+)\[(\w+)\]\s*=\s*\{(.*?)\};", re.S)


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


def _strip_comments(source: str) -> str:
    """The source without // and /* */ comments; string literals are kept as they are."""
    return _STRING_OR_COMMENT.sub(lambda m: m.group(0) if m.group(0).startswith('"') else "", source)


def _string_literal(token: str, where: str) -> str | None:
    """The text of a "..." literal, None when `token` is not one."""
    if not (len(token) >= 2 and token.startswith('"') and token.endswith('"')):
        return None
    if "\\" in token:
        raise ValueError(f"{where}: escape sequences are not supported in {token}")
    return token[1:-1]


def _only_separators(text: str, where: str) -> None:
    """Fails unless `text` (what is left of a table body once its entries are taken out) is commas and space."""
    if text.replace(",", "").strip():
        raise ValueError(f"{where}: cannot read {text.replace(',', ' ').strip()!r}")


class _Header:
    """The constants and tables of the catalogue headers, read with regexes. Anything it cannot read raises."""

    def __init__(self):
        self.files: list[str] = []
        self.defined_in: dict[str, str] = {}
        self.ints: dict[str, int] = {}
        self.strings: dict[str, str] = {}
        self.int_arrays: dict[str, list[int]] = {}
        self.pair_tables: dict[str, list[tuple[int | str, int | str]]] = {}
        self.string_tables: dict[str, list[str]] = {}

    def _define(self, table: dict, name: str, value, where: str) -> None:
        kinds = (self.ints, self.strings, self.int_arrays, self.pair_tables, self.string_tables)
        if any(name in kind for kind in kinds):
            raise ValueError(f"{where}: {name} is defined twice (first in {self.defined_in[name]})")
        table[name] = value
        self.defined_in[name] = where

    def _int(self, token: str, where: str) -> int:
        if re.fullmatch(r"-?\d+", token):
            return int(token)
        if token in self.ints:
            return self.ints[token]
        raise ValueError(f"{where}: cannot read {token!r} as an int literal or a known int constant")

    def _value(self, token: str, where: str) -> int | str:
        """An int literal or constant, or a string literal or constant."""
        text = _string_literal(token, where)
        if text is not None:
            return text
        if token in self.strings:
            return self.strings[token]
        try:
            return self._int(token, where)
        except ValueError:
            raise ValueError(f"{where}: cannot read {token!r} as a literal or a known constant") from None

    def _sized(self, size: str, items: list, name: str, where: str) -> list:
        declared = self._int(size, f"{where}: {name}")
        if declared != len(items):
            raise ValueError(f"{where}: {name} declares {declared} entries, found {len(items)}")
        return items

    def read(self, path: Path) -> None:
        where = path.name
        self.files.append(where)
        text = path.read_text(encoding="utf-8")
        if "'" in _STRING_OR_COMMENT.sub("", text):
            # The string and comment stripper does not know char literals: a '"' would derail it.
            raise ValueError(f"{where}: char literals (') are not supported outside strings and comments")
        source = _strip_comments(text)
        for name, value in _INT_CONSTANT.findall(source):
            self._define(self.ints, name, self._int(value.strip(), f"{where}: {name}"), where)
        for name, value in _STRING_CONSTANT.findall(source):
            text = _string_literal(value.strip(), f"{where}: {name}")
            if text is None:
                raise ValueError(f"{where}: {name} = {value.strip()} is not a string literal")
            self._define(self.strings, name, text, where)
        for size, name, body in _INT_ARRAY.findall(source):
            items = [self._int(item.strip(), f"{where}: {name}") for item in body.split(",") if item.strip()]
            self._define(self.int_arrays, name, self._sized(size, items, name, where), where)
        for size, name, body in _PAIR_TABLE.findall(source):
            entries = []
            for entry in _PAIR.findall(body):
                tokens = [token.strip() for token in entry.split(",")]
                if len(tokens) != 2:
                    raise ValueError(f"{where}: {name}: cannot read entry {{{entry}}}")
                entries.append(tuple(self._value(token, f"{where}: {name}") for token in tokens))
            _only_separators(_PAIR.sub("", body), f"{where}: {name}")
            self._define(self.pair_tables, name, self._sized(size, entries, name, where), where)
        for name, size, body in _STRING_TABLE.findall(source):
            items = [_string_literal(item, f"{where}: {name}") for item in _STRING.findall(body)]
            _only_separators(_STRING.sub("", body), f"{where}: {name}")
            self._define(self.string_tables, name, self._sized(size, items, name, where), where)

    def get(self, table: dict, name: str):
        if name not in table:
            files = ", ".join(self.files)
            raise ValueError(f"no {name} in the parsed headers ({files}) that the page catalogue can read")
        return table[name]


def _names_by(header: _Header, name: str, key_type: type) -> dict:
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
    """The known-property catalogue (.specs/sniffer.md), read from sec_touch's protocol_ids.h and fan_level.h, plus
    the default discover range and the goal names, read from the sniffer's property_slots.h and scan_goal.h."""
    header = _Header()
    header.read(header_dir / FAN_LEVEL_FILE)
    header.read(header_dir / PROTOCOL_IDS_FILE)
    header.read(sniffer_header_dir / PROPERTY_SLOTS_FILE)
    header.read(sniffer_header_dir / SCAN_GOAL_FILE)
    return {
        "levelIds": header.get(header.int_arrays, "FAN_LEVEL_IDS"),
        "labelIds": header.get(header.int_arrays, "FAN_LABEL_IDS"),
        "summerVentilationId": header.get(header.ints, "SUMMER_VENTILATION_PROPERTY_ID"),
        "dateId": header.get(header.ints, "DATE_PROPERTY_ID"),
        "clockId": header.get(header.ints, "CLOCK_PROPERTY_ID"),
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
