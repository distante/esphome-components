# PR visuals

Smallest view that makes the point; usually one, rarely more than two. Place each next to its caption. Keep only the calls, files, states, and boundaries the reader needs.

## Journeys (What changes for you)

Owner's or developer's steps, before/after; the same shapes fit both. Owner vocabulary: panel, fan, level, Home Assistant, web page. Developer vocabulary: the files, commands and tools they touch (`CLAUDE.md`, a skill, `pre-commit`, a test suite).

Fix → before/after flowchart:

```mermaid
flowchart LR
    subgraph before[Before]
        a1[Owner sets fan to level 4] --> a2[Panel sends a garbled reply] --> a3[Home Assistant shows a wrong level]
    end
    subgraph after[After]
        b1[Owner sets fan to level 4] --> b2[Garbled reply is dropped] --> b3[Home Assistant keeps the last good level]
    end
```

New or changed owner task → `journey`:

```mermaid
journey
    title Mapping an unknown property
    section Set up
      Add the sniffer to the YAML: 3: Owner
      Flash the device: 3: Owner
    section Sniff
      Open the sniffer web page: 5: Owner
      Change a setting on the panel: 4: Owner
      See which property changed: 5: Owner
```

Owner, HA, and panel interacting → sequence diagram:

```mermaid
sequenceDiagram
    actor Owner
    participant HA as Home Assistant
    participant Panel
    Owner->>HA: press "Sync clock"
    HA->>Panel: write the time
    Panel-->>HA: acknowledge
    HA-->>Owner: clock drift back to 0 s
```

## Technical views (Technical summary)

Logic → pseudocode:

```text
on(message)
  if CRC does not match
    record TIMEOUT for the task
    return
  apply value
```

Runtime control flow → call tree:

```text
SECTouchComponent::loop
  read_uart
    parse_message
      check_crc
  process_task
```

File responsibility / broad refactor → shallow file tree:

```text
components/sec_touch/
├── sec_touch.cpp     # hub: UART, task queue
├── fan/              # fan platform
└── text_sensor/      # labels and raw strings
```

Interaction / data flow → Mermaid:

```mermaid
sequenceDiagram
    participant Hub
    participant Panel
    Hub->>Panel: GET property
    Panel-->>Hub: value + CRC
    Hub->>Hub: verify CRC
```

Changed existing shape → `diff` of that shape.

File layout:

```diff
 tests/config/valid/
   sec_touch_fan_only.yaml
+  sec_touch_all_platforms.yaml   # every platform at once
```

Call tree:

```diff
 parse_message
+  check_crc
   apply_value
```

Control flow:

```diff
 on(message)
+  if CRC does not match
+    record TIMEOUT
+    return
   apply value
```

Mostly new, or omitted context hides ownership/order → full block.

---

Technical views adapted from [Dex Horthy](https://github.com/dexhorthy)'s [`show-me`](https://github.com/humanlayer/skills/blob/main/plugins/show-me/skills/show-me/SKILL.md) skill (HumanLayer).
