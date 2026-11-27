---
name: review
description: "Use before or while reviewing a diff in this repo, and when briefing a reviewer subagent."
---

How to run a review, and the checks a reviewer runs on a diff. Each check enforces a rule from [coding-standards](../coding-standards/SKILL.md) or [sec-touch](../sec-touch/SKILL.md) and links to it; the rule lives there.

## Running a review

1. Start every reviewer and subagent prompt with this line, plus the issue and spec: `Read .claude/skills/coding-standards/SKILL.md first; it is, or links to, every standard of this repo.`
2. Give the reviewer the whole diff and every acceptance criterion. Known gaps go in as facts ("criterion 6 is deferred to #34"), and the reviewer decides whether to flag them.
3. Run every check below and report each hit with `file:line` and what it breaks. Have each reviewer brief say to run them too.
4. Fix every hard violation a reviewer reports, or tell the user why not. Only findings the reviewer itself called judgement calls may be skipped.

Done when every reported finding is fixed, or named to the user with a reason.

## No duplicated code, rules or IDs in component and test code

Enforces [Reuse existing definitions](../coding-standards/SKILL.md#reuse-existing-definitions) in `components/` and `tests/`.

1. Run [Checklist: before coding](../coding-standards/SKILL.md#checklist-before-coding) step 3 over every literal, list, bound and helper the diff adds under `components/` and `tests/`. Example: a hard-coded `0-255` while `MAX_PROPERTY_ID` exists.
2. Also look for a second statement of a rule or composition, and for two hand-rolled implementations of one thing, test helpers included. Examples: a `static_assert` summing the known-ID arrays that `is_known_property()` already names; two bounded digit parsers; the same frame builder in two test files.
3. Apply the tests clause of [Reuse existing definitions](../coding-standards/SKILL.md#reuse-existing-definitions) to every expected value the diff adds in tests, the test's own constants included. Example: an expected `"id":77` in a test that feeds in `PROBE_ID`, defined as 77.

Done when each hit is reported with its `file:line` and the existing definition it duplicates.

## Removed state has no reader left

Enforces [Tests](../coding-standards/SKILL.md#tests): a removal is a change, so it lands with a test or with proof that nothing reads what it removed.

1. For every assignment, reset or field the diff removes because "no test can see it" or "it is redundant", grep every reader of that state, not only the main computation. Include paths that copy the state and override part of it before reading. Example: a start's `scan_completed_ = false` reset looked redundant because `done` is `!active && completed`, but the stop reply forces `active = false` on a copy and then read the stale `completed` as `done: true`.
2. For each reader the removal changes, ask for a test that reads the state through that path.

Done when every removed piece of state has its readers listed, and each reader the removal changes has a test.

## Every task end reaches its listeners

Enforces [Error handling](../coding-standards/SKILL.md#c-style): a task that ends any way but the usual one still leaves no state behind.

1. List every path in the diff that ends, drops, clears or replaces a queued or in-flight task: answer, NAK, timeout, retry give-up, scan entry and exit, queue clear. Check that each one tells the task-end listeners. Example: `exit_scan_mode` reset the in-flight task without notifying, so a bulk call stayed `busy` and a pending mask read blocked every later switch write until reboot.
2. For each pending flag or busy state the diff adds, name the task end that clears it on every path in step 1. A state with no clearing end on some path is a finding.
3. For each path that skips a step the usual end performs (a read-back, a compare), check that the outcome reports that the step was skipped, not success.

Done when each end path has its listener call, and each pending state has a clearing end on every path.

## Every codegen path builds next to every platform

Enforces the Config compile layer of [Tests](../coding-standards/SKILL.md#tests): a codegen path has a fixture that builds it.

1. For each `to_code` path the diff adds or changes, find a compiled valid fixture that enables it together with every platform of the same component. Example: time-program codegen called `select.new_select`, but once a YAML also had `select: - platform: sec_touch`, Python bound the package's `select` to `sec_touch/select/` and the build crashed; the time-program fixtures had no sec_touch platforms and `sec_touch_all_platforms` had no time programs.
2. Check that no module name a component's `__init__.py` imports matches one of its own sub-folders or `.py` files.

Done when each codegen path names its fixture, and each import that shares a name with a sub-module is reported with its `file:line`.

## A per-entry validation reports its own entry

Enforces the YAML config layer of [Tests](../coding-standards/SKILL.md#tests): the error lands on the entry that is wrong, and a fixture pins where.

1. For each validation the diff adds that ESPHome runs once per entry but that reads every entry (a `FINAL_VALIDATE_SCHEMA` over `fv.full_config`, a read of `CORE.raw_config`), check that it raises only for the entry it is validating. Example: the duplicate `fan_number` check scanned the whole `fan:` list, so the error showed under the non-duplicate fan 2 entry.
2. Check that its invalid fixture puts a valid entry first and has an `# expect-error:` line with `<fixture>.yaml:<line>]`, the source line ESPHome prints for an offending entry.

Done when each such validation is reported with its `file:line` and whether it raises for other entries, and its fixture pins the location.

## A result that goes back to unknown clears what it showed

Enforces [Error handling](../coding-standards/SKILL.md#c-style): a state that turns unknown leaves no stale value on display.

1. List every function the diff lets return "nothing" (nullopt, no state, an empty result) after it returned a value, such as a model that loses a needed input.
2. For each, follow its caller and check that the entities or state built from the earlier value are cleared, and that any timer armed from it is disarmed. Example: after live state began needing only some inputs, enabling a switch time whose time never read well made `compute()` return nullopt, and `recompute_()` returned early, leaving `not running` shown.

Done when each listed function has its caller traced, and each stale display is reported with its `file:line`.

## A published number fits its type

Enforces [Error handling](../coding-standards/SKILL.md#c-style): an entity shows the value the code computed.

1. For each value the diff publishes through a `float` (a `sensor` state) or converts to a narrower type, take the largest value its range allows and check that the type holds it to the precision the entity promises. Example: `next_change` published a Unix timestamp (about 1.8e9) as a sensor `float`, which holds steps of 128 s there, so 14:00:00 showed as 14:00:48 (#148).

Done when each such value names its range and the type's step at that range, and each loss is reported with its `file:line`.

## No restated rules in skills and docs for agents

Enforces [coding-standards](../coding-standards/SKILL.md)' single source (every rule lives there once) and [Reuse existing definitions](../coding-standards/SKILL.md#reuse-existing-definitions).

1. When the diff touches `.claude/skills/`, `CLAUDE.md` or `.specs/`, grep [coding-standards](../coding-standards/SKILL.md) and [sec-touch](../sec-touch/SKILL.md) for each added instruction.
2. An instruction that is already a rule there becomes a link to it, never a second wording. Example: a review check that repeated coding-standards' "grep for the concept by names, synonyms and value" step.
3. Apply [Reuse existing definitions](../coding-standards/SKILL.md#reuse-existing-definitions) to each code value written out. Example: a range written out in sec-touch's fan_number bullet while it lives in `FAN_PAIR_COUNT`.

Done when each added instruction has a grep behind it, each restatement is reported with its `file:line` and the rule it restates, and each written-out code value is reported with its `file:line` and the definition to name.

## Stored state reads well in a later version

Enforces [Stored state](../coding-standards/SKILL.md#stored-state).

1. List everything the diff writes to browser storage or flash.
2. For each, picture a later version that adds a value (a new type, status, option) and a user whose storage the current version wrote. The new value must take its default. Example: chips stored as the ticked list leave a later-added chip unticked.

Done when each stored item is listed with what a later-added value gets.

## Page JS and HTML are formatted by hand

Enforces the Page JS and HTML bullet of [Tooling](../coding-standards/SKILL.md#tooling): no formatter checks these lines.

1. Read every added or changed line in `components/*/web/` against its neighbours: spacing after commas, wrapping, indentation. Example: `download(CSV_FILE_NAME,exportCsv(` missing the space after the comma.

Done when each hit is reported with its `file:line`.

## Every comment is an allowed kind

Enforces [Code comments](../coding-standards/SKILL.md#code-comments).

1. Read every comment the diff adds or changes in `components/` and `tests/` against the rule. Example: #149 tagged seven tests `// #139 item N:` and explained the late clock read on its constant, two methods and a test.

Done when each hit is reported with its `file:line`, the bullet it breaks, and its trimmed text or "delete".

## Docs and PR text are short and visual

Enforces [Docs and PR text](../coding-standards/SKILL.md#docs-and-pr-text) on the lines the diff adds or changes in every `.md`, plus the PR body and issue comments. Its bullets are the criteria.

1. Read each file against every bullet of the rule. Examples of hits: a "How it was confirmed" paragraph per field, where a table with one row per part and a ✅ / ⚠️ column holds the same facts; columns of offsets aligned with spaces in a text block, where a table or Mermaid diagram renders.
2. For a rewrite, set the old text beside the new and check that every fact of the old text survives.

Done when each hit is reported with its `file:line`, the bullet it breaks and its fix (for structure: the table or Mermaid diagram that replaces it).

## Stated facts match their source

Enforces the "Facts match their source" bullet of [Docs and PR text](../coding-standards/SKILL.md#docs-and-pr-text) on every `.md`, PR body and issue comment the diff adds or changes.

1. Recount every count, total and size the text states (writes per call, entities per pair, IDs per block) from the parts it lists elsewhere. Example: a spec's "2 of 16 writes failed" while its own write order sends 5 times, 5 levels and one mask: 11 SETs.
2. Look up every config key the text calls optional, recommended or required in its schema: the component's `__init__.py`, or the vendored ESPHome under `esphome/`. Example: "ideally with `relation: child`" while ESPHome 2026.11.0 has `relation` as `cv.Required`.

Done when each stated number has its recount and each key its schema line, and each mismatch is reported with its `file:line` and the source it contradicts.

## Every doc names an ID's current readers

Enforces the "Facts match their source" bullet of [Docs and PR text](../coding-standards/SKILL.md#docs-and-pr-text) for docs the diff leaves untouched.

1. List every property ID whose pollers, readers or writers the diff changes (a feature starts or stops polling it, a listener moves, a write path is added).
2. For each, grep every `.md` in the repo (readmes, `RAW_VALUES.md`, `.specs/`, skills) for the ID and its name, and read each mention's claim about who polls, reads or writes it. Example: live state stopped polling the panel date, yet `RAW_VALUES.md` and the readme's "Date and Clock" line still named only `clock_drift` as reading IDs 1 and 2.

Done when each listed ID has its grep, and each mention naming old readers or writers is reported with its `file:line`.
