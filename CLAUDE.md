# ESPHome Components — Development Guidelines

## C++ brace style

Always wrap `if`/`else`/`for`/`while` bodies in braces `{}`, even single-line bodies. Never use brace-free single-line control flow.

## Early exit

Always exit early when a condition makes the rest of a function irrelevant. Check preconditions and guard clauses at the top and return immediately. Avoid wrapping the main logic in an `if` block when an inverted early return keeps the happy path at the lowest indentation level.

## Tests

Commands and setup: [readme.md#tests](readme.md#tests). Every change lands with tests in one of three layers:

- **C++ logic**: plain C++ with no ESPHome includes, so it builds on the host. When logic needs `esphome/core` types, cut a seam rather than stubbing ESPHome. Suites live in `tests/cpp/test_<name>/`.
- **YAML config**: fixtures in `tests/config/valid/` or `tests/config/invalid/`. Each invalid fixture carries a `# expect-error: <text>` line.
- **Page JS**: DOM-free ES modules in `components/<component>/web/`, imported by the page, with `*.test.mjs` beside them.
