---
name: review
description: "Use before or while reviewing a diff in this repo, and when briefing a reviewer subagent."
---

Checks a reviewer runs on a diff. Each check enforces a rule from [coding-standards](../coding-standards/SKILL.md) or [sec-touch](../sec-touch/SKILL.md) and links to it; the rule lives there. Run every check, then report each hit with `file:line` and what it breaks.

## No duplicated code, rules or IDs in component code

Enforces [Reuse existing definitions](../coding-standards/SKILL.md#reuse-existing-definitions) in `components/`.

1. For every literal, list, bound or helper the diff adds under `components/`, grep `components/` for an existing name of the same concept, by name, synonyms and value. Example: a hard-coded `0-255` in an error message while `MAX_PROPERTY_ID` exists.
2. Look for a second statement of a rule or composition defined elsewhere. Example: a `static_assert` summing `FAN_LEVEL_IDS` + `FAN_LABEL_IDS` + `GLOBAL_SETTING_IDS` restates what `is_known_property()` calls known.
3. Look for two hand-rolled implementations of one thing. Example: two bounded digit parsers.
4. For each value Python and C++ both need, confirm one side reads it from the other. Example: `text_sensor.py` reads `MAX_PROPERTY_ID` from `property_slots.h`; `page_bundle.py` parses `protocol_ids.h`.

Done when every added literal, list, bound and helper has a grep behind it, and each duplicate is reported with its `file:line` and the existing definition it duplicates.
