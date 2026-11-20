# ESPHome Components — Development Guidelines

## C++ brace style

Always wrap `if`/`else`/`for`/`while` bodies in braces `{}`, even single-line bodies. Never use brace-free single-line control flow.

## Early exit

Always exit early when a condition makes the rest of a function irrelevant. Check preconditions and guard clauses at the top and return immediately. Avoid wrapping the main logic in an `if` block when an inverted early return keeps the happy path at the lowest indentation level.

## Reuse existing definitions

Before adding a constant, enum, type or helper, search `components/` and `tests/` for one that already names the concept, and use it. Each value has a single source of truth: code and tests reference its name, never a bare literal or a second definition. When the existing definition sits behind an ESPHome include that a pure header must not pull in, move it into a pure header that both sides include.

## SEC-Touch protocol

The SEC-Touch hardware and its UART protocol are fixed: command IDs, frame layout, property IDs and level values stay as they are. Code may rely on protocol constants coinciding (a SET and a data frame both start with `32`). Judge such code on readability alone.

## Worktrees

Work in a git worktree (`EnterWorktree`, or `isolation: "worktree"` for subagents) unless the user says not to. Worktrees live in `.claude/worktrees/`.

## Formatting and linting

Before committing, run `pre-commit run --files <changed files>` and keep its fixes. Setup: [readme.md#linting](readme.md#linting). Never format with a clang-format from outside the venv or pre-commit: other versions format differently.

## Tests

Commands and setup: [readme.md#tests](readme.md#tests). Every change lands with tests in one of five layers:

- **C++ logic** (the default for logic): plain C++ with no ESPHome includes, so it builds on the host. When logic needs `esphome/core` types, cut a seam: a pure header the component calls. Suites live in `tests/cpp/test_<name>/`.
- **Host component**: the real component and real ESPHome core, compiled for the `host` platform. Reach for it only for whole-component behaviour no seam can hold: bytes in through a fake UART, `loop()`, bytes and callbacks out. Fake only the bus; everything else is the real ESPHome build. Suites live in `tests/host/<component>/`.
- **YAML config**: fixtures in `tests/config/valid/` or `tests/config/invalid/`. Each invalid fixture carries a `# expect-error: <text>` line.
- **Config compile**: every valid fixture is also built with `esphome compile` (opt-in `--compile`). Use it for include and `AUTO_LOAD` bugs: add a valid fixture that leaves out the component the C++ wrongly needs. It also fails on compiler warnings from `components/`, so every valid fixture keeps `logger: level: VERY_VERBOSE`.
- **Page JS**: DOM-free ES modules in `components/<component>/web/`, imported by the page, with `*.test.mjs` beside them.

## Pull requests

All PR rules: [.claude/skills/pr-body/SKILL.md](.claude/skills/pr-body/SKILL.md). Read it before opening a PR or writing a PR title or body.
