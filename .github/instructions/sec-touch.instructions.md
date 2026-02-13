---
name: 'Sec-Touch component instructions'
description: 'Coding conventions Sec-Touch component for ESPHome'
applyTo: 'components/sec_touch/**'
---

# Sec-Touch component instructions

- Treat [components/sec_touch/readme.md](components/sec_touch/readme.md) as the authoritative spec for protocol details, special values, and hardware mapping; do not duplicate or contradict it here.
- Keep the UART handshake intact: ACK handling and GET/SET sequencing are required; changes to UART parsing must preserve ACK behavior.
- Respect the update flows: fan levels are polled via recursive tasks, labels are refreshed only by manual update tasks (triggered by the program text update button).
- Validate IDs before queueing tasks; task creation returns null for unknown IDs and must be handled.
- Schema constraints are strict: fan_number is 1-6, text_sensor requires mode_text while label_text is optional; keep these aligned with ID arrays.
- If modifying fan behavior, keep preset-to-speed mapping consistent with special values and do not change the meaning of speed 255 (not connected).
