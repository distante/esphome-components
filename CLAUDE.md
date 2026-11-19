# ESPHome Components — Development Guidelines

## C++ brace style

Always wrap `if`/`else`/`for`/`while` bodies in braces `{}`, even single-line bodies. Never use brace-free single-line control flow.

## Early exit

Always exit early when a condition makes the rest of a function irrelevant. Check preconditions and guard clauses at the top and return immediately. Avoid wrapping the main logic in an `if` block when an inverted early return keeps the happy path at the lowest indentation level.

## Worktrees

Work in a git worktree (`EnterWorktree`, or `isolation: "worktree"` for subagents) unless the user says not to. Worktrees live in `.claude/worktrees/`.

## Tests

Commands and setup: [readme.md#tests](readme.md#tests). Every change lands with tests in one of five layers:

- **C++ logic** (the default for logic): plain C++ with no ESPHome includes, so it builds on the host. When logic needs `esphome/core` types, cut a seam: a pure header the component calls. Suites live in `tests/cpp/test_<name>/`.
- **Host component**: the real component and real ESPHome core, compiled for the `host` platform. Reach for it only for whole-component behaviour no seam can hold: bytes in through a fake UART, `loop()`, bytes and callbacks out. Fake only the bus; everything else is the real ESPHome build. Suites live in `tests/host/<component>/`.
- **YAML config**: fixtures in `tests/config/valid/` or `tests/config/invalid/`. Each invalid fixture carries a `# expect-error: <text>` line.
- **Config compile**: every valid fixture is also built with `esphome compile` (opt-in `--compile`). Use it for include and `AUTO_LOAD` bugs: add a valid fixture that leaves out the component the C++ wrongly needs. It also fails on compiler warnings from `components/`, so every valid fixture keeps `logger: level: VERY_VERBOSE`.
- **Page JS**: DOM-free ES modules in `components/<component>/web/`, imported by the page, with `*.test.mjs` beside them.

## Pull requests

All PR rules: [.claude/skills/pr-body/SKILL.md](.claude/skills/pr-body/SKILL.md). Read it before opening a PR or writing a PR title or body.
