---
name: coding-standards
description: "Use before planning, changing, reviewing or committing anything in this repo, however small, and before a PR or briefing a subagent."
---

Single source for this repo's coding standards. Every rule lives here once; `CLAUDE.md`, `readme.md` and `.github/skills/code-review/` (for Copilot) point here. PR title and body rules live in [pr-body](../pr-body/SKILL.md).

## Precedence

1. **Repo rules beat generic skill rules.** When a skill (TDD's anti-tautology rule, a review skill's defaults) disagrees with a rule here, follow the rule here and tell the user which skill rule you set aside.
2. **Spec versus repo rule: ask.** When a spec in `.specs/` or an issue asks for something a rule here forbids (the spec says "allocated once in `setup()`", the Memory rule says static), ask the user which wins before writing code.
3. **A rule you disagree with still binds.** Follow it and raise the disagreement with the user.

## Checklist: before coding

1. Enter a worktree ([Tooling](#tooling)).
2. Read the issue with its acceptance criteria (`gh issue view <n>`) and its spec in `.specs/` when one exists. List every spec-versus-rule conflict and ask the user about each.
3. Before adding any constant, enum, type or helper, grep `components/` and `tests/` for the **concept**, by its names and synonyms as well as its value, then apply [Reuse existing definitions](#reuse-existing-definitions). A value Python and C++ both need gets one source (such as a codegen define from Python); a "same as X" comment is a second definition.
4. For each heap allocation the change adds, use static or stack allocation instead, or tell the user why the heap is needed ([Memory](#memory)).
5. Pick the test layer and write the test first ([Tests](#tests)).
6. Before each commit, run pre-commit and commit with the hooks running ([Tooling](#tooling)).

Done when each new name in the diff has a grep behind it and each added heap allocation has a stated reason.

## Checklist: review

1. Start every reviewer and subagent prompt with this line, plus the issue and spec: `Read .claude/skills/coding-standards/SKILL.md first; it is the complete list of this repo's standards.`
2. Give the reviewer the whole diff and every acceptance criterion. Known gaps go in as facts ("criterion 6 is deferred to #34"), and the reviewer decides whether to flag them.
3. Fix every hard violation a reviewer reports, or tell the user why not. Only findings the reviewer itself called judgement calls may be skipped.

Done when every reported finding is fixed, or named to the user with a reason.

## Checklist: before the PR

1. Go through the issue's acceptance criteria one by one and mark each **met** or **partial**.
2. Any criterion partial: list it in the PR body and write `Refs #N`. `Closes #N` is for an issue whose every criterion is met.
3. Name every deviation from the spec in the PR body.
4. Follow [pr-body](../pr-body/SKILL.md) for title and body.

Done when every criterion carries a mark and the closing keyword matches the marks.

## Rules

### Repository

- This is an ESPHome custom component repository. All custom components are located in the `components/` directory. Each component should have its own `readme.md` file.
- Contributions follow the [ESPHome contribution guidelines](https://esphome.io/guides/contributing.html), and all code adheres to the [ESPHome coding standards](https://esphome.io/guides/contributing.html#coding-standards).
- Domain terms, and the words to avoid for each: [GLOSSARY.md](../../../GLOSSARY.md).
- Feature specs live in `.specs/`.

### C++ style

- Follow the ESPHome C++ coding conventions: <https://developers.esphome.io/>.
- **Braces.** Always wrap `if`/`else`/`for`/`while` bodies in braces `{}`, even single-line bodies. Never use brace-free single-line control flow.
- **Early exit.** Always exit early when a condition makes the rest of a function irrelevant. Check preconditions and guard clauses at the top and return immediately. Avoid wrapping the main logic in an `if` block when an inverted early return keeps the happy path at the lowest indentation level.
- **Error handling.** Implement robust error handling to ensure that the component can gracefully handle unexpected conditions.
- Formatting is `.clang-format`, applied by pre-commit ([Tooling](#tooling)).

### Memory

Be mindful of memory usage, especially on devices with limited resources. Avoid dynamic memory allocation where possible and use static or stack allocation.

### Reuse existing definitions

Before adding a constant, enum, type or helper, search `components/` and `tests/` for one that already names the concept, and use it. Each value has a single source of truth: code and tests reference its name, never a bare literal or a second definition. When the existing definition sits behind an ESPHome include that a pure header must not pull in, move it into a pure header that both sides include.

This binds tests too: a test's expected values use the shared names (a cap, a separator, an ellipsis) where a generic testing skill would ask for an independent literal ([Precedence](#precedence)).

### SEC-Touch protocol

The SEC-Touch hardware and its UART protocol are fixed: command IDs, frame layout, property IDs and level values stay as they are. Code may rely on protocol constants coinciding (a SET and a data frame both start with `32`). Judge such code on readability alone.

### `components/sec_touch/**` only

These apply when a change touches `components/sec_touch/`.

- Treat [components/sec_touch/readme.md](../../../components/sec_touch/readme.md) as the authoritative spec for protocol details, special values, and hardware mapping; do not duplicate or contradict it.
- Keep the UART handshake intact: ACK handling and GET/SET sequencing are required; changes to UART parsing must preserve ACK behavior.
- Respect the update flows: fan levels are polled via recursive tasks, labels are refreshed only by manual update tasks (triggered by the program text update button).
- Validate IDs before queueing tasks; task creation returns null for unknown IDs and must be handled.
- Schema constraints are strict: fan_number is 1-6, text_sensor requires mode_text while label_text is optional; keep these aligned with ID arrays.
- If modifying fan behavior, keep preset-to-speed mapping consistent with special values and do not change the meaning of speed 255 (not connected).

### Tests

Commands and setup: [readme.md#tests](../../../readme.md#tests). Every change lands with tests in one of five layers:

- **C++ logic** (the default for logic): plain C++ with no ESPHome includes, so it builds on the host. When logic needs `esphome/core` types, cut a seam: a pure header the component calls. Suites live in `tests/cpp/test_<name>/`.
- **Host component**: the real component and real ESPHome core, compiled for the `host` platform. Reach for it only for whole-component behaviour no seam can hold: bytes in through a fake UART, `loop()`, bytes and callbacks out. Fake only the bus; everything else is the real ESPHome build. Suites live in `tests/host/<component>/`.
- **YAML config**: fixtures in `tests/config/valid/` or `tests/config/invalid/`. Each invalid fixture carries a `# expect-error: <text>` line. A valid fixture's name starts with its component's folder name (`sec_touch_sniffer_with_time.yaml`).
- **Config compile**: every valid fixture is also built with `esphome compile` (opt-in `--compile`). Use it for include and `AUTO_LOAD` bugs: add a valid fixture that leaves out the component the C++ wrongly needs. It also fails on compiler warnings from `components/`, so every valid fixture keeps `logger: level: VERY_VERBOSE`.
- **Page JS**: DOM-free ES modules in `components/<component>/web/`, imported by the page, with `*.test.mjs` beside them.

**TDD with teeth.** Every change follows it:

1. Define the test (if a new API is needed, define it with a skeleton).
2. See the test go red due to a real assertion (not a build problem).
3. Implement the fix and see the test go green.
4. Remove the implementation: the test goes red again with the correct assertion error.
5. Restore the implementation.

### Tooling

- **Worktrees.** Work in a git worktree (`EnterWorktree`, or `isolation: "worktree"` for subagents) unless the user says not to. Worktrees live in `.claude/worktrees/`.
- **Formatting and linting.** Before committing, run `pre-commit run --files <changed files>` and keep its fixes. Setup: [readme.md#linting](../../../readme.md#linting). Never format with a clang-format from outside the venv or pre-commit: other versions format differently.
- **Git hooks.** Commit with the hooks running. Never bypass them: no `--no-verify`, no `PRE_COMMIT_ALLOW_NO_CONFIG`.
