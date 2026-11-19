---
name: pr-body
description: "Use when opening a pull request, or writing or updating a PR title or body."
---

Single source for all PR rules in this repo.

## Steps

1. **Gather.** Issue: number from branch name (`fix/43-x` → `gh issue view 43`). Commits: `git log main..HEAD`. Diff: `git diff main...HEAD`. Existing PR: `gh pr view`. Done when every changed file has a known reason.
2. **Title.** `<type>(<component>): <change>`, conventional commit, matching branch commits. No scope for repo-wide work.
3. **Body.** Fill the template into a scratchpad file. Done when each section is filled or omitted by its rule.
4. **Confirm.** Show title and body to the user. Wait for approval.
5. **Publish.** Push with upstream. `gh pr create --base main --title … --body-file …`, or `gh pr edit <n> …` if the PR exists. Reply with the URL.

## Template

```markdown
## TL;DR

<1–3 sentences, non-technical>

## What changes for you

<user journey diagram>

## Technical summary

<diagram, diff-sketch, or tree>

## Evidence

- **Before:** <failing run/output>
  **After:** <passing run/output>
- **Teeth:** <implementation removed → test red with the right assertion>

## Merge Danger

**Door:** <one-way | two-way>

<optional: why>

**Blast Radius:** <one word>

<optional: what could break>

Closes #<issue>
```

## Rules

No preambles. Terms from [GLOSSARY.md](../../../GLOSSARY.md).

- **TL;DR**: for the panel owner, not a developer. What they notice: what works now, what stops failing, what YAML they must change. No identifiers, files, or bytes. No owner-visible change (tests, CI, docs) → one sentence naming who benefits.
- **What changes for you**: owner journey before/after (actions, what panel/HA show). Omit the section when owner experience is unchanged. Shapes: [visuals.md](visuals.md).
- **Technical summary**: reviewer view. Smallest view that shows the change, one-line caption each. Mermaid for flow/interaction, `diff` for a changed existing shape. Menu: [visuals.md](visuals.md).
- **Evidence**: screenshot if visible (web page, HA); else the test red → green with command and output. Teeth run required (TDD with teeth): removed line, failing assertion, restored.
- **Door**: two-way = revert undoes it. One-way = not (renamed YAML key, changed default, panel writes); state why.
- **Blast radius**: widest breakable scope (`tests`, `hub`, `fan`, `config`, `all owners`), then what could break.
- **Closes**: omit when the branch has no issue.
