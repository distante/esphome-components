---
name: coding-standards
description: "Use before planning, changing, reviewing or committing anything in this repo, however small, and before a PR or briefing a subagent."
---

Single source for this repo's coding standards; every rule lives here once. Elsewhere:

- `components/sec_touch*/` rules: [sec-touch](../sec-touch/SKILL.md)
- PR title and body: [pr-body](../pr-body/SKILL.md)
- Review procedure and checks: [review](../review/SKILL.md)
- Test and lint commands: [readme.md](../../../readme.md#tests)
- Domain terms: [GLOSSARY.md](../../../GLOSSARY.md)
- Feature specs: `.specs/`

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

Follow [review](../review/SKILL.md).

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

### C++ style

- Follow the ESPHome C++ coding conventions: <https://developers.esphome.io/>.
- **Braces.** Always wrap `if`/`else`/`for`/`while` bodies in braces `{}`, even single-line bodies. Never use brace-free single-line control flow.
- **Early exit.** Always exit early when a condition makes the rest of a function irrelevant. Check preconditions and guard clauses at the top and return immediately. Avoid wrapping the main logic in an `if` block when an inverted early return keeps the happy path at the lowest indentation level.
- **Error handling.** Implement robust error handling to ensure that the component can gracefully handle unexpected conditions.
- Formatting is `.clang-format`, applied by pre-commit ([Tooling](#tooling)).

### Memory

Be mindful of memory usage, especially on devices with limited resources. Avoid dynamic memory allocation where possible and use static or stack allocation. Exception: `components/sec_touch_sniffer/` ([sec-touch](../sec-touch/SKILL.md)).

### Stored state

Data that outlives a version (browser storage, flash) is read later by code that knows more values than the code that wrote it. Store the exceptions to the default, so a value a later version adds takes its default for a returning user. Example: the sniffer stores the unticked filter chips, so a new chip starts ticked; storing the ticked ones would start it unticked.

### Reuse existing definitions

Before adding a constant, enum, type or helper, search `components/` and `tests/` for one that already names the concept, and use it. Each value has a single source of truth: code and tests reference its name, never a bare literal or a second definition. A composition (which arrays make up a set) and an algorithm (a parser) are definitions too: state each once. When the existing definition sits behind an ESPHome include that a pure header must not pull in, move it into a pure header that both sides include.

This binds tests too: a test's expected values use the shared names (a cap, a separator, an ellipsis) where a generic testing skill would ask for an independent literal ([Precedence](#precedence)).

### Tests

Every change lands with tests in one of five layers:

- **C++ logic** (the default for logic): plain C++ with no ESPHome includes, so it builds on the host. When logic needs `esphome/core` types, cut a seam: a pure header the component calls. Suites live in `tests/cpp/test_<name>/`.
- **Host component**: the real component and real ESPHome core, compiled for the `host` platform. Reach for it only for whole-component behaviour no seam can hold: bytes in through a fake UART, `loop()`, bytes and callbacks out. Fake only the bus; everything else is the real ESPHome build. Suites live in `tests/host/<component>/`.
- **YAML config**: fixtures in `tests/config/valid/` or `tests/config/invalid/`. Each invalid fixture carries a `# expect-error: <text>` line. A valid fixture's name starts with its component's folder name (`sec_touch_sniffer_with_time.yaml`).
- **Config compile**: every valid fixture is also built with `esphome compile` (opt-in `--compile`). Use it for include and `AUTO_LOAD` bugs: add a valid fixture that leaves out the component the C++ wrongly needs. It also fails on compiler warnings from `components/`, so every valid fixture keeps `logger: level: VERY_VERBOSE`.
- **Page JS**: DOM-free ES modules in `components/<component>/web/`, imported by the page, with `*.test.mjs` beside them. The build inlines every module into the page's one module script (`components/sec_touch_sniffer/page_bundle.py`), so only `import { a, b } from "./x.mjs";` (no `as`) and `export function` / `async function` / `const` / `let` / `class` work, and a top-level name must be unique across the page and all its modules. The rendering glue in the page itself has no tests; keep logic out of it. To preview the page without flashing, see [readme.md](../../../readme.md#tests).

**TDD with teeth.** Every change follows it:

1. Define the test (if a new API is needed, define it with a skeleton).
2. See the test go red due to a real assertion (not a build problem).
3. Implement the fix and see the test go green.
4. Remove the implementation: the test goes red again with the correct assertion error.
5. Restore the implementation.

### Tooling

- **Worktrees.** Work in a git worktree (`EnterWorktree`, or `isolation: "worktree"` for subagents) unless the user says not to. Worktrees live in the main checkout's `.claude/worktrees/`.
  - **Starting a task while already in a worktree** (a session reused with `/clear`, for example): when that worktree holds no earlier work on this task, which is the usual case, call `ExitWorktree` with `action: "keep"` first, then `EnterWorktree` with a new `name`. `EnterWorktree` refuses to create a worktree from inside another, and one made by hand from there nests inside it. Stay in the current worktree only when it already holds this task's own earlier work.
- **Formatting and linting.** Before committing, run `pre-commit run --files <changed files>` and keep its fixes. Never format with a clang-format from outside the venv or pre-commit: other versions format differently.
- **Page JS and HTML** have no formatter in pre-commit. Format them by hand like the surrounding code: a space after each comma, the same wrapping and indentation.
- **Git hooks.** Commit with the hooks running. Never bypass them: no `--no-verify`, no `PRE_COMMIT_ALLOW_NO_CONFIG`.

### Docs and PR text

Readmes, reports, PR bodies and issue comments are read by a skimming human and by agents paying per token. Write them short and visual:

- **Show structure.** A layout, a mapping, a before/after or a status per item goes in a table or a Mermaid diagram. A code block holds only code, pseudocode, a call tree or a file tree. Prose carries only what has no structure: a reason, a caveat.
- **Plain words.** Use a technical term only when it names the thing (an ID, a panel label, a protocol constant).
- **One fact, one place.** A value sits in one table; other places link to it.
- **Short sentences**, one idea per bullet.
- **Keep every fact.** Shortening drops words, never data.
- **Raw dumps stay out of the repo.** An export or a full log becomes a table of the values that matter.
- **Facts match their source.** A count or total adds up from the parts the text itself lists. A config key the text calls optional, recommended or required is that in its schema (`cv.Optional` / `cv.Required`).
