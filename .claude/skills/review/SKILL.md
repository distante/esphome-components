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

## No restated rules in skills and docs for agents

Enforces [coding-standards](../coding-standards/SKILL.md)' single source: every rule lives there once.

1. When the diff touches `.claude/skills/`, `CLAUDE.md` or `.specs/`, grep [coding-standards](../coding-standards/SKILL.md) and [sec-touch](../sec-touch/SKILL.md) for each added instruction.
2. An instruction that is already a rule there becomes a link to it, never a second wording. Example: a review check that repeated coding-standards' "grep for the concept by names, synonyms and value" step.

Done when each added instruction has a grep behind it, and each restatement is reported with its `file:line` and the rule it restates.

## Docs and PR text are short and visual

Enforces [Docs and PR text](../coding-standards/SKILL.md#docs-and-pr-text) on the lines the diff adds or changes in every `.md`, plus the PR body and issue comments. Its bullets are the criteria.

1. Read each file against every bullet of the rule. Examples of hits: a "How it was confirmed" paragraph per field, where a table with one row per part and a ✅ / ⚠️ column holds the same facts; columns of offsets aligned with spaces in a text block, where a table or Mermaid diagram renders.
2. For a rewrite, set the old text beside the new and check that every fact of the old text survives.

Done when each hit is reported with its `file:line`, the bullet it breaks and its fix (for structure: the table or Mermaid diagram that replaces it).
