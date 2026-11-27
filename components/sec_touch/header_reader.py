"""Reads the constants and tables of this component's plain C++ headers, with regexes and no compiler.

Two readers use it: this component's codegen (__init__.py takes SWITCH_TIME_COUNT from protocol_ids.h) and the
sniffer's page bundle (../sec_touch_sniffer/page_bundle.py builds the page catalogue from them). Either way,
the header stays each value's one source.

Each declaration read from a header must keep one of these forms:

- `constexpr [const] int` or `constexpr [const] uint8_t` set to a (possibly negative) decimal or a hex (`0x0A`) int
  literal, or an earlier such constant
- `constexpr const char *` set to a string literal
- a `std::array` of ints or of {value, name} pairs
- the NAME_MAPPING string table

Anything a reader uses that it cannot read raises. An int constant it cannot read (an expression such as
`FRAME_BUFFER_SIZE - 1`) raises only once a reader, a table or another constant uses it. Its limits, in every header
it reads:

- no comma inside the string of a {value, name} entry: the entry is split at commas;
- no constexpr locals of those forms in a function body: the declaration regexes would read them as constants;
- no char literals ('x') or digit separators: a ' outside strings and comments raises.
"""

from pathlib import Path
import re

_STRING_OR_COMMENT = re.compile(r'"(?:[^"\\\n]|\\.)*"|//[^\n]*|/\*.*?\*/', re.S)
_STRING = re.compile(r'"(?:[^"\\\n]|\\.)*"')
_INT_CONSTANT = re.compile(r"\bconstexpr\s+(?:const\s+)?(?:int|uint8_t)\s+(\w+)\s*=\s*([^;]*);")
_STRING_CONSTANT = re.compile(r"\bconstexpr\s+const\s+char\s*\*\s*(\w+)\s*=\s*([^;]*);")
_INT_ARRAY = re.compile(r"\bconstexpr\s+(?:const\s+)?std::array<int,\s*(\w+)>\s+(\w+)\s*=\s*\{([^{}]*)\};")
_PAIR_TABLE = re.compile(r"\bconstexpr\s+(?:const\s+)?std::array<\w+,\s*(\w+)>\s+(\w+)\s*=\s*\{\{(.*?)\}\};", re.S)
_PAIR = re.compile(r"\{([^{}]*)\}")
_STRING_TABLE = re.compile(r"\bconstexpr\s+const\s+char\s*\*\s*(\w+)\[(\w+)\]\s*=\s*\{(.*?)\};", re.S)


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


class HeaderReader:
    """The constants and tables of the headers it has read. Anything it cannot read raises."""

    def __init__(self):
        self.files: list[str] = []
        self.defined_in: dict[str, str] = {}
        self.ints: dict[str, int] = {}
        # Int constants it cannot read, with why: they fail only once something uses them.
        self.unreadable_ints: dict[str, str] = {}
        self.strings: dict[str, str] = {}
        self.int_arrays: dict[str, list[int]] = {}
        self.pair_tables: dict[str, list[tuple[int | str, int | str]]] = {}
        self.string_tables: dict[str, list[str]] = {}

    def _define(self, table: dict, name: str, value, where: str) -> None:
        kinds = (self.ints, self.unreadable_ints, self.strings, self.int_arrays, self.pair_tables, self.string_tables)
        if any(name in kind for kind in kinds):
            raise ValueError(f"{where}: {name} is defined twice (first in {self.defined_in[name]})")
        table[name] = value
        self.defined_in[name] = where

    def _int(self, token: str, where: str) -> int:
        if re.fullmatch(r"-?\d+", token):
            return int(token)
        if re.fullmatch(r"-?0[xX][0-9a-fA-F]+", token):
            return int(token, 16)
        if token in self.ints:
            return self.ints[token]
        if token in self.unreadable_ints:
            raise ValueError(f"{where}: uses {token}, which it cannot read ({self.unreadable_ints[token]})")
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
            try:
                number = self._int(value.strip(), f"{where}: {name}")
            except ValueError as error:
                self._define(self.unreadable_ints, name, str(error), where)
                continue
            self._define(self.ints, name, number, where)
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
        if table is self.ints and name in self.unreadable_ints:
            raise ValueError(self.unreadable_ints[name])
        if name not in table:
            files = ", ".join(self.files)
            raise ValueError(f"no {name} in the parsed headers ({files}) that it can read")
        return table[name]
