---
name: sec-touch
description: "Use before planning, changing or reviewing anything under components/sec_touch/ or components/sec_touch_sniffer/."
---

SEC-Touch rules, on top of [coding-standards](../coding-standards/SKILL.md). The sniffer is part of SEC-Touch: it is the development tool used to map the panel's properties.

## Protocol (both components)

The SEC-Touch hardware and its UART protocol are fixed: command IDs, frame layout, property IDs and level values stay as they are. Code may rely on protocol constants coinciding (a SET and a data frame both start with `32`). Judge such code on readability alone.

## `components/sec_touch/**`

- Treat [components/sec_touch/readme.md](../../../components/sec_touch/readme.md) as the authoritative spec for protocol details, special values, and hardware mapping; do not duplicate or contradict it. If you have tangible proof that it is wrong, ask the user whether to update it.
- Changes to UART parsing must keep the ACK handshake and GET/SET sequencing the readme describes.
- Respect the update flows: fan levels are polled via recursive tasks, labels are refreshed only by manual update tasks (triggered by the program text update button).
- Validate IDs before queueing tasks; task creation returns null for unknown IDs and must be handled.
- Schema constraints are strict: fan_number is 1-6, text_sensor requires mode_text while label_text is optional; keep these aligned with ID arrays.
- If modifying fan behavior, keep the preset-to-speed mapping consistent with the readme's special level values, `255` included.

## `components/sec_touch_sniffer/**`

**Memory.** The sniffer is a development tool, not production code, so the coding-standards [Memory](../coding-standards/SKILL.md#memory) rule is relaxed here: heap allocation is allowed. What matters is that the ESP32 does not crash while the sniffer runs.
