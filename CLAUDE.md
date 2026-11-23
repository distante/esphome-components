# ESPHome Components — Development Guidelines

All coding standards live in the `coding-standards` skill (`.claude/skills/coding-standards/SKILL.md`). Load it before planning, changing, reviewing or committing anything here, however small. Brief every subagent and reviewer as the review skill's "Running a review" says (`.claude/skills/review/SKILL.md`).

Before sending any request to a real SEC-Touch device, load the `sec-touch` skill (`.claude/skills/sec-touch/SKILL.md`, "Real device"): it says which requests are never allowed.

When the user or a later review finds an issue that earlier review rounds missed, ask the user with AskUserQuestion whether to add a check for it to the review skill (`.claude/skills/review/SKILL.md`).
