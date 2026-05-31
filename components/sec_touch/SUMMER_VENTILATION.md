# Sniffer Report: Summer Ventilation — Attribute 48

**Date:** 2026-05-30
**Device:** `esp-ventilation-controller` (sec_touch protocol)
**Method:** 4 sniffer captures (2× OFF, 2× ON) across full property range 1–208

---

## Finding

**Property ID `48` controls Summer Ventilation.**

| State | Value |
|-------|-------|
| OFF   | `800` |
| ON    | `0`   |

Consistent across all four captures. No other property changed reliably between states.

---

## Semantics

This is a **global setting** — it is not per-fan and applies to the entire controller.

---

## Implementation Notes

- **Property ID:** `48`
- **Type:** numeric (integer), range observed: `0`–`800`
- **Write to activate summer ventilation:** set property `48` to `0`
- **Write to deactivate summer ventilation:** set property `48` to `800` (default/restore value)
- No per-fan targeting needed — a single write to property 48 affects the whole system
- Should be exposed as a global entity in the `group_settings` web server sorting group, not attached to any fan group

---

## Source Logs

| File | State |
|------|-------|
| [OFF_1.log](OFF_1.log) | OFF (first capture) |
| [OFF_2.log](OFF_2.log) | OFF (second capture) |
| [ON_1.log](ON_1.log)  | ON (first capture)  |
| [ON_2.log](ON_2.log)  | ON (second capture) |
