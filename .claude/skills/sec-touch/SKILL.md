---
name: sec-touch
description: "Use before planning, changing or reviewing anything under components/sec_touch/ or components/sec_touch_sniffer/, and before sending any request to a real SEC-Touch device or its sniffer."
---

SEC-Touch rules, on top of [coding-standards](../coding-standards/SKILL.md). The sniffer is part of SEC-Touch: it is the development tool used to map the panel's properties.

## Real device

**Never send data to a property ID that is not known.** On a real controller, never send a write probe (`POST /sniffer/probe`), a restore (`POST /sniffer/restore`) or any SET for a property ID outside the known-property catalogue (`components/sec_touch/protocol_ids.h`). That holds even for a value the device should refuse (too long, a forbidden byte): validation is code that can fail, and an unknown property may control anything. Write to an unknown ID only when the user names the ID and the value in the conversation. To test a refusal on hardware, use a known ID, which the device refuses before anything else.

Reads are fine: GET `/sniffer/data`, the page, the logs, and discover scans (they only query).

### Driving it over REST

URLs come from ESPHome's `web_server` and the sniffer's paths (`sniffer_data.h`); look them up there. What they don't tell you:

- **A pair's mode goes through its mode select**, not the fan: web_server's fan `turn_on` ignores `preset_mode` and still answers 200. The select exists only where the YAML adds one ([readme](../../../components/sec_touch/readme.md), `split_special_modes`).
- **A fan speed also switches the pair to Normal**, ending Automatik Zeit.
- **IDs 209–255 time out**: a discover scan of 1–208 covers everything in about 2 min; 1–255 takes 5½.
- **Check a write landed** by reading the pair's level ID in `/sniffer/data`; its `hist` shows when it changed.

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
