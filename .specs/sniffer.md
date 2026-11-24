# Sniffer / Listener Mode

## Goal

Capture what the SEC-Touch panel sends, **as raw strings**, so reverse-engineers can do two things, the two **sniffer goals**:

- **Discover** (the default): find and map **unknown properties**.
- **Check**: confirm the panel sends what the component expects for its **known properties**.

In discover, **experiments** guide the search: the user captures the panel at named values of one setting, the page narrows the unknown properties down to the **candidates** that track it, and a **write probe** tests whether a corroborated candidate can be written.

A light HTML page served by the ESP is the only interface: it starts scans, runs experiments and write probes, views, classifies, annotates and exports the data.

Background: v1.4.0 showed that property values are strings (`11.11.26`, `SWZ0021E21H`, `-50.0`, `0.360.00:00`), see [`RAW_VALUES.md`](../components/sec_touch/RAW_VALUES.md). The previous sniffer only saw `atoi` results.

Terms (property, raw value, property record, change, query status, known / registered / unknown property, sniffer goal, experiment, capture, label, candidate, corroborated candidate, disqualified property, write probe, verdict, restore) are defined in [`GLOSSARY.md`](../GLOSSARY.md). Each section links the decision that holds its full reasoning: the [raw-value map](https://github.com/distante/esphome-components/issues/22), the [sniffer-goals map](https://github.com/distante/esphome-components/issues/67) and the [experiments map](https://github.com/distante/esphome-components/issues/95).

**Platform: ESP32 only.** `sec_touch_sniffer` fails config validation on any other target. There is no fallback to the old int sniffer.

---

## Behaviour

### Sniffer goals

Decided while charting the [sniffer-goals map](https://github.com/distante/esphome-components/issues/67).

One goal drives both the scan and the page:

| | Discover | Check |
|---|---|---|
| Scan queries | the range sent with the start, **minus known properties** | **every known property**, registered ones included, in ascending ID order; no range |
| Page shows | the ID map without known properties | one card per fan plus a Global card |

- The device **records every property** whatever the goal. The goal only picks what is queried and shown.
- **Known** is fixed by the component ([Known-property catalogue](#known-property-catalogue)), not by the YAML. **Registered** is a subset of known.
- There is no pass/fail validation in check: it shows each value and its expected meaning.

### Passive capture

The sniffer observes **every** data message, known and registered property IDs included. Registered components (e.g. fan controls) keep working normally. Regular polling keeps running in passive mode, so registered IDs are seen through it.

### Active scan

Decided in [How do the sniffer goal and the catalogue reach the device–page contract?](https://github.com/distante/esphome-components/issues/70). Scans start and stop **only from the page**. There is no switch.

**When started** (with a goal):
- Regular polling stops completely. Tasks already queued (e.g. pending fan SET commands) are discarded.
- Registered component listeners stop receiving updates.
- The sniffer queries the goal's IDs one at a time, from the goal's first ID. A discover start with an **ID list** (a narrowed capture, [contract](#devicepage-contract)) queries only the listed IDs, in ascending order. The next query is issued only after the previous answer (or timeout) is processed.
- In discover, known IDs are skipped. Their query status is left as it is.

**When stopped**, or when the run completes:
- Scanning stops immediately.
- Regular polling resumes at once, without waiting for the next poll cycle.

**One run at a time.** The device has one exclusive slot for a scan (plain or capture), a write probe and a restore. A start of any of them while another runs is refused with 409 ([contract](#devicepage-contract)). Decided in [How do capture scans and write probes reach the device–page contract?](https://github.com/distante/esphome-components/issues/99).

**No resume.** Every start begins at the goal's first ID; stop means abort. A full pass is the reverse-engineering loop (change the setting on the panel, capture again, see which [candidates](#experiments) track it), and a resumed scan would not give one. A discover run takes ~1–2 min (160 ms + round trip per answered ID); an unnarrowed 1–255 pass can take ~7 min longer when 208–255 never answer (~9 s per timed-out ID).

### Write probe

Decided in [How does a write probe run, and how does it fail safely?](https://github.com/distante/esphome-components/issues/98), with the contract and the SET retry from [How do capture scans and write probes reach the device–page contract?](https://github.com/distante/esphome-components/issues/99). The device runs a write probe as **one request** ("probe ID X with value V") on its main loop, restore included, so closing the tab or losing WiFi mid-probe still restores. It knows nothing about experiments: which values may be probed is the page's rule ([Experiments](#experiments)).

- **Exclusive, polling paused.** A probe takes the run slot and runs in scan mode, so regular polling is paused until it ends.
- **No confirmation, no stop.** Once started, a probe always runs to its end (under a minute, about 50 s with every retry). A stop request during a probe or restore is a 200 no-op.

**Steps**
1. **Original**: GET the ID, with the scan's one retry. That value is the original and the restore target. If the GET times out or NAKs, or the original already equals the value, the probe ends with verdict **nothing written**.
2. **SET** the value. The panel's answer (ACK = `ok`, NAK, timeout) is recorded. A timed-out SET is **retried once**, after the same 5 s wait as a scan GET ([Scan timeout and retry](#scan-timeout-and-retry)); both attempts are recorded.
3. **Read-back**: GET right after the answer, then about every 1 s until it equals the value or 5 s pass. A timed-out GET is one failed attempt and polling goes on. Every read-back is a step.
4. **Verdict**:
   - **writable**: a read-back equals the value.
   - **not writable**: the SET got ACK or NAK and every read-back shows the original.
   - **unclear**: anything else (the retried SET timed out too, every read-back timed out, or a read-back shows a third value).
5. **Restore**: writable and unclear restore unless the last read-back equals the original; not writable never needs it. Without a restore, `restore` is `skipped`, as it is for nothing written. To restore: SET the original (retried once on timeout), then verify with the same ~5 s polling.
6. **Restore failed**: when the verification doesn't show the original, send the restoring SET once more and verify again. If that fails too, `restore` is `failed` ("ID X is now Y, was Z") and the sniffer logs an ERROR.

**Restore request.** A separate request runs only steps 5–6 for a given ID and original: a SET of the original, the ~5 s verify, one more SET and verify. The page sends it from the "restore" button after a failed restore or an interrupted probe; it supplies the original, because after a reboot the device no longer knows it.

**Result.** The device keeps the last probe's or restore's result, with every step, in the `probe` object of `/sniffer/data` until the next one starts. It lives outside the record store, so Clear data keeps it and a probe goes on through a clear; a reboot loses it. The probe's GETs pass the message tap like any data message, so they update the ID's record too.

---

## Property records

Decided in [What does the sniffer record per property?](https://github.com/distante/esphome-components/issues/24).

One **property record** per property ID:

| Field | Meaning |
|---|---|
| raw value | byte-exact, padding included (`"00               "` stays as is). The page trims it for display. |
| command id | the latest only. All messages so far carry `32`; kept so an unexpected id shows up. |
| `first_seen` | first message for this ID |
| `last_seen` | updated on every message, even when nothing changed |
| `last_changed` | when the current raw value was first seen |
| `change_count` | starts at 0 (the first sighting is not a change); unbounded |
| query status + `status_at` | `unqueried` / `ok` / `nak` / `timeout` |
| history | previous raw values with the time each was first seen |
| registered flag | read from the hub when data is served, **not stored** |

- **Change:** the raw bytes differ. A different command id alone is not a change.
- **Query status** is set only by the sniffer's own scan queries, in either goal. Passive messages never touch it. `timeout` means no answer after the retry; `nak` means the panel refused the ID.
- A **failure** updates status and `status_at` only. The last good value, history and `change_count` stay, and failures never enter the history.
- A record that only ever failed has no raw value, empty history, and unset `first_seen` / `last_seen` / `last_changed`.
- **Timestamps** are device uptime. No RTC is needed: the page renders wall time as `browser_now - (uptime_now - t)`. The sniffer does not use `time:`.
- Records carry **no known flag**: the page derives it from the catalogue module.

---

## Known-property catalogue

Decided in [Where does the known-property catalogue live, and how does the page decode known values?](https://github.com/distante/esphome-components/issues/68).

**One source.** Two pure C++ headers in `components/sec_touch/` (no ESPHome includes) hold the known property IDs and their meaning tables: `protocol_ids.h`, already pure and holding `FAN_LEVEL_IDS` and `FAN_LABEL_IDS`, grows into the catalogue, and the level and preset names sit in `fan_level.h`, beside the level constants they are keyed by. Each value is still defined once. The component, the sniffer and the page all read them; nothing is copied by hand.

| Known property | IDs | Expected meaning |
|---|---|---|
| Fan level | 173–178 (fan 1–6) | level name for 0–11 and 255 |
| Fan label | 78–83 (fan 1–6) | `NAME_MAPPING` name for 0–68 |
| Summer ventilation | 48 | `0A00` → on, `0800` → off |
| Date | 1 | the value as sent |
| Clock | 2 | the value as sent |

- **Moves into the headers:** `GLOBAL_SETTING_IDS`, the 48/1/2 ID constants and `NAME_MAPPING` out of `_definitions.h` into `protocol_ids.h`, and the six preset names of `FanModeList` (`fan/_fan_mode.cpp`, behind an ESPHome include) into `components/sec_touch/fan_level.h` (moved out of `fan/`, which ESPHome copies only into builds that load the fan platform), beside the level constants they are keyed by. `fan_level.h` also names levels 1–6 and 0 (off) and 255 (not connected), which have no names today. The component keeps using them from there.
- **Left as is:** the Python copy of the fan IDs in `sec_touch/__init__.py`.
- **Matching.** Levels and labels match the trimmed raw value read as a whole decimal integer. Property 48 matches the raw string `0A00`/`0800`, not the `0`/`800` the component's `atoi` parse yields. Date and clock always match and show the trimmed value.
- **Unexpected value.** A known property whose value matches no entry shows as `unexpected value x` (`x` the trimmed raw value). That also covers corrupted values. There are no further decode rules.
- **Device:** the sniffer uses the known-ID list to pick what a scan queries.
- **Page:** `page_bundle.py` parses both headers at every build and generates a **virtual** module, `catalogue.mjs`, with the IDs and meanings. The resolver serves that name from memory instead of `web/`; no file is written. Only `sniffer_page.html` imports it. Tested modules (export, check cards, discover filtering) take the catalogue as an argument, so their `node --test` suites pass a fixture. A Python test covers the header parse.

---

## Memory caps

Decided in [What are the sniffer's memory caps?](https://github.com/distante/esphome-components/issues/31), sized against [measured heap](https://github.com/distante/esphome-components/issues/30#issuecomment-6911212958) on a real controller (BLE proxy, `web_server`, ESP-IDF 5.5.6): ~57 KB min free, ~31 KB largest free block.

**Budget:** ~27 KB, allocated once in `setup()`. No per-ID allocations and no single block over 16 KB. All caps are named constants in the header, not YAML options.

| What | Cap | Cost |
|---|---|---|
| Property IDs | 256 slots, indexed by ID 0–255 | — |
| Record fields | 4 × u32 uptime **seconds**, u32 `change_count`, u32 `seq` (for the page's deltas), cmd, status, raw length, flags (32 B) | 256 × 32 B = 8 KB (one block) |
| Raw value | 59 B, never truncated | 256 × 59 B ≈ 15 KB (one block) |
| History | 2 KB pool, depth 8 per ID (current value included) | 2 KB |
| Serving scratch | binary record copy ≤ ~540 B + 256 B JSON chunk | ~800 B, static |
| Write probe result | ID, value and original (≤ 15 B each), verdict, restore, 32 steps × {kind, outcome, value ≤ 15 B, at} | ~900 B, static |
| Pending request and running scan | the pending ID set and the running scan's ID set, 256-bit sets (32 B each), plus a value ≤ 15 B | ~80 B, static |

- **Probe steps:** 32 slots, sized in [How do the decisions become the updated sniffer spec, and how is it sliced?](https://github.com/distante/esphome-components/issues/101). Steps past the 32nd are not recorded. The sequence still runs to its end, and the verdict and restore are worked out as usual.
- **IDs above 255** are dropped, counted (`dropped` in the data JSON) and logged once. A discover range above 255 is a 400 ([contract](#devicepage-contract)).
- **Timestamps** are stored as u32 uptime seconds (good for 136 years) and sent to the page as ms (`s × 1000`).
- **Raw cap 59 B** is everything the hub's 64 B frame buffer (`IncomingMessage::buffer`, `FRAME_BUFFER_SIZE`) can deliver: the hub routes a frame with empty command and property IDs and no CRC, so only STX, two TABs and ETX surround the value. The cap is derived from `FRAME_BUFFER_SIZE`, the constant the buffer is declared with, so they can't drift. The longest raw value seen so far is 17 B.
- **History pool:** one 2 KB array of variable-length entries `{id, len, t, bytes}` (~12 B typical, ~170 entries). It holds **previous values only**; the current value lives in the record and its time is `last_changed`.
  - An ID keeps at most 7 previous values. Inserting another drops that ID's oldest.
  - When the pool is full, drop the oldest entry of the ID with the most entries, so a fast-changing ID (the clock, ID 2) can't push out the history of rare IDs.
  - Each insert costs one memmove of ≤ 2 KB.

---

## Configuration

Decided in [How does the sniffer retire its text sensor?](https://github.com/distante/esphome-components/issues/72), from the research in [How do ESPHome components deprecate or move a platform or config key?](https://github.com/distante/esphome-components/issues/71).

```yaml
web_server_base:   # or web_server:

sec_touch_sniffer:
```

- **Entry point:** a top-level `sec_touch_sniffer:` block, its `CONFIG_SCHEMA` in `components/sec_touch_sniffer/__init__.py`. Single instance (no `MULTI_CONF`): the page has one `/sniffer` URL.
- **IDs, both optional:** `id` is `cv.GenerateID()`, since nothing references the sniffer. `sec_touch_id` is `cv.GenerateID(CONF_SEC_TOUCH_ID): cv.use_id(SECTouchComponent)`, so the only hub is picked automatically. A bare `sec_touch_sniffer:` is a full config.
- **No other keys.** The goal, the scan and the discover range all live on the page. No `cv.invalid` keys for retired options.
- **HTTP server required:** final validation fails without `web_server_base` loaded, with "add `web_server_base:` (or `web_server:`)". The page is always compiled in.
- **ESP32 only**: any other target is a config error.
- **Auth and port** are inherited. The handler registers with `add_handler()`, so `web_server: auth:` protects it when set. The port is `web_server_base`'s. The sniffer has no auth or port options.
- **Sockets**: `web_server_base.add_httpd_clients(2, "sec_touch_sniffer")`: one for the polling tab, one spare for a reload or keep-alive. It calls `socket.consume_sockets(2, "sec_touch_sniffer")` and also raises the httpd client limit (`max_open_sockets`) to match, as `web_server` does for its own clients.

**The old platform is a hard error.** `text_sensor.py` is reduced to `CONFIG_SCHEMA = cv.invalid(...)`, as ESPHome did for dallas and udp, with no warning release. The old form shipped in v1.4.0 only, and a warning period would keep the `TextSensor` base, `scan_switch` and the YAML range alive. One message covers the whole old platform (`scan_switch`, `scan_start`/`scan_end`, `web_page`, `name`, `time_id` alike):

```
sec_touch_sniffer moved out of text_sensor in v1.5.0.
Replace the text_sensor entry with:

  sec_touch_sniffer:
  web_server_base:   # or web_server:

scan_switch, scan_start/scan_end and web_page are gone:
start scans and pick the range on the page at /sniffer.
<link to readme>
```

**Removed with the text sensor:** the `TextSensor` base, the state builder (`sniffer_state.h`) and its 2048-char state, the scan switch (`sec_touch_sniffer_scan_switch.h`), `set_web_page()` and the `USE_SEC_TOUCH_SNIFFER_WEB_PAGE` define, `set_scan_range()`, and the saved scan position.

---

## Page

Decided in [What should the sniffer HTML page look like and do?](https://github.com/distante/esphome-components/issues/26) and, for the goals, [What does the sniffer page look like in check, and how does the user switch goals?](https://github.com/distante/esphome-components/issues/69). The prototypes live on the throwaway branches [`prototype/sniffer-page`](https://github.com/distante/esphome-components/blob/prototype/sniffer-page/components/sec_touch_sniffer/prototype/sniffer_page.prototype.html) and [`prototype/sniffer-goals`](https://github.com/distante/esphome-components/blob/prototype/sniffer-goals/components/sec_touch_sniffer/prototype/goals.prototype.html) (variant C in both). Experiments were decided in [What does the experiment panel look like in discover?](https://github.com/distante/esphome-components/issues/97), prototyped on the throwaway branch [`prototype/experiment-panel`](https://github.com/distante/esphome-components/blob/prototype/experiment-panel/components/sec_touch_sniffer/web/PROTOTYPE_experiment_panel.html) (variant C, the coach). Audience: reverse-engineers. Function over polish, desktop only. On narrow screens the side column stacks below the grid; no further mobile work.

### Goal switch

- **Big page tabs** under the header, full width: "Discover — find unknown properties" and "Check — confirm known properties".
- The page **always opens in discover**. The goal is not stored in the browser.
- The scan button names the goal ("▶ Start discover scan"), and its label says what the scan queries.
- **Switching while a scan runs** asks for confirmation ("Switching stops the running scan"), then stops it. The user starts the new goal's scan by hand. The page never changes a running scan's goal. A probe or restore can't be stopped: switching goals while one runs just shows it as busy.
- A scan the page did not start (another tab) is labelled with its own goal from `scan.goal`.

### Scan control

- Start/stop button with a progress bar and label: "Scanning ID n of a–b", "stopped at n", or "complete". Check counts its progress against the known IDs from the catalogue module. A capture uses the same row: "capturing "boost" for fan 1 mode · 34/239 · full range" (or "· N candidates").
- **Busy.** A start refused with 409 (another tab's scan, capture, probe or restore runs) shows "busy: <what runs>" next to the button and starts nothing. While the device reports a run, the scan button is **Stop** during a scan or capture and disabled during a probe or restore. Capture and every Write probe and restore button are disabled.
- **Discover range:** a from/to input next to the start button, sent with the start. The page fills it with the last range used (browser storage, keyed by MAC), or 1–255 the first time. A 400 shows as an error next to the input.

### Check

**Panel-style cards**, no ID map, no filters, no change feed: 15 IDs fit on one screen.
- **One card per fan**: the level meaning in large type, the raw `id = value` under it, then the label meaning and its raw value. A `registered` badge marks fans this YAML uses (`reg`).
- **One Global card**: summer ventilation, date and clock, each with meaning and raw value.
- An unexpected value shows in red as `unexpected value x`. A known property with no value yet shows as not seen.

### Discover

Today's page minus known properties, plus [experiments](#experiments).
- **ID map**: a 16×16 grid of IDs 0–255 with **known IDs as blank gaps**; a "show known" checkbox (off by default, [stored per browser](#browser-storage)) shows them greyed with their value, and their meaning in the tooltip, still outside the filters, feed and experiments. Each cell shows its ID and the start of its trimmed raw value, coloured by guessed type or by scan status (user's choice). A cell flashes on change. A ring shows the open experiment's candidate state: **orange** = candidate, **green** = corroborated; the legend shows both. The scan cursor is outlined while a scan runs. A never-seen ID has a white cell with a dashed border, so it does not look like an `unqueried` one. The grid fills the width left of the side column and sizes its rows to the window height, so all 256 cells fit without scrolling.
- **Drawer** (click a cell): raw value (trimmed, with a marker when it was padded on the wire), type, query status, change count, last change, history (newest first) and a note field.
- **Change feed**: recent changes across unknown IDs (`time ID old → new`), newest first. Clicking an entry selects its ID.
- **Per-ID mute**: a drawer button removes the ID from the feed and from experiments (a muted ID is never a candidate); its cell still updates.
- **Filters**: search (ID, value, note), **type chips** and **status chips** on one line (type │ status), changed recently, and **"only candidates"** (the open experiment's candidates and corroborated candidates). They fade and stripe non-matching cells and apply to the feed except "only candidates"; the chip group "colour by" is not using shows faded swatches but still filters. "Hide registered" is gone: registered properties are known, so discover never shows them.
- **No experiment open:** there are no rings, and "only candidates" is disabled.
- **Plain scan stays.** "Start scan" scans the discover range with no label, for browsing the grid and feed. It never becomes a capture: only Capture adds to an experiment.
- There is **no "changed vs previous capture"** ring, flash or filter: the candidate rules and the label → raw tables explain what changed, and the feed keeps raw changes.

### Experiments

Decided in [What makes an ID a candidate in an experiment, and when is it corroborated?](https://github.com/distante/esphome-components/issues/96) (the rules) and [What does the experiment panel look like in discover?](https://github.com/distante/esphome-components/issues/97) (the panel). Experiments replace the mark. They live in the browser only ([Browser storage](#browser-storage)); the device sees a capture as an ordinary discover scan, with an ID list when narrowed.

**Switching experiments.** A row of **sub-tabs** under the goal tabs, in discover only: one tab per experiment with its candidate count, and a last tab **"+ new experiment"**, where starting one means naming the setting (e.g. "fan 1 mode"). The open sub-tab is the open experiment.

**Coach box**, at the top of the side column, above the drawer:
- A **next-step prompt** worked out from the experiment's state, steering toward a round trip and then corroboration:

  | State | Prompt |
  |---|---|
  | no capture | set the panel to any value, name it, capture |
  | one capture | change to a different value |
  | two captures, no candidate | go back to <first label> and capture: a round trip finds candidates |
  | candidates, none corroborated | capture <label> again to corroborate |
  | a corroborated candidate | run a write probe, or capture other values |
  | every candidate disqualified | the next capture scans the full range again |

- A large **label input** that suggests labels already used, **Capture**, and a **"full rescan"** link that makes the next capture full.
- A line **"next capture: full range F–T"** or **"next capture: N candidates"**.

**Captures.**
- Capture starts a discover scan of the experiment's **full range** while it has no candidates, and of **only its current candidates** once any exist (a disqualified ID drops out). If every candidate is disqualified, the next capture is full again by itself; "full rescan" forces a full one. With more than 100 candidates the page captures the full range instead ([contract](#devicepage-contract)).
- **Full range** = the discover range at the experiment's **first capture**, stored with it. Changing the from–to input later doesn't change it.
- A capture is stored when its run completes (`done`) and stores, for each **queried** ID, its query status and raw value. A run that is stopped (by the user or another tab) stores **nothing**. When `boot` changes (a reboot or Clear data), the page **abandons** a capture in flight and stores nothing, even though the device's scan goes on after a clear. Either way the label stays in the input, so pressing Capture again retries it.
- A start refused with 409 stores nothing; a 200 means the next finished run is this page's own.
- **Labels** are the same label after trimming outer whitespace, ignoring case. The page shows the first spelling used.
- A **timeline** under the candidate cards lists the captures newest first: label, time, "full" or "N IDs" (a narrowed capture gets a hollow dot), and **delete**. Deleting is also how a mistyped label is fixed: delete it and capture again.

**Candidates**, derived from the stored captures every time they change; nothing is sticky:
- **Walk**: an ID's captures in order, keeping those where it has an entry. A **timeout** and a capture that **didn't query** the ID are skipped. A **nak** stays in the walk but is not a value.
- **Candidate**: an unknown, unmuted ID whose walk holds three consecutive entries `X = v`, `Y = w`, `X = v`, with Y a different label and `w ≠ v` byte for byte. Any labels can fill X and Y. A nak between two entries breaks the round trip. `A, B, C` with `01, 02, 03` has no return and is not a candidate; an ID that changes for only some labels (boost = `03`, normal = eco = `01`) can be.
- **Consistency**: every capture with the same label must give the ID the same raw value. One disagreeing capture **disqualifies** the ID in this experiment (a counter `A=1, B=2, A=3` is out at its third capture). There is no noise list.
- **Corroborated**: a candidate where two labels have each been captured at least twice with agreeing values (e.g. `A, B, A, B`).
- Deleting a capture can bring a disqualified ID back. It then has no entries in the narrowed captures that skipped it; a full rescan fills those gaps.

**Candidate cards**, below the coach box, corroborated first: the ID (click to select it), a **candidate** / **corroborated** badge, its current raw value, and the **label → raw value table** with how often each label was seen. A line **"N disqualified"** expands to the IDs and their conflicting values per label.

**Write probe on a card** ([Write probe](#write-probe)):
- Only a **corroborated** candidate's card offers it: a select of the raw values **already observed for that ID in this experiment's captures**, at most 15 bytes and without NUL, TAB, STX or ETX, defaulting to one other than the current value, and a **Write probe** button that starts it straight away. A plain candidate's card says "write probe once corroborated". This "only observed values" rule is enforced by the page alone.
- The steps (SET → ACK/NAK/timeout, GET → value, restore) and the verdict (✓ writable, ✗ not writable, and unclear or nothing written as words) stay on the card and are **appended to the ID's note**.
- **Restore failed** shows "restore failed: ID X is now Y, was Z" on the card and in the note, with a **restore Z** button (the restore request).
- **Interrupted**: before the request, the page stores "probe pending: ID X, writing V" in browser storage per MAC, and the original once the device reports it. If the device then reports no such probe (a reboot lost it), the card shows "probe interrupted: ID X may still be V, original Z" with the restore button. The pending entry is cleared when a result arrives, or when the start is refused with 400 or 409. The page judges this from the `probe` object, not from `boot`, since Clear data draws a new `boot` but keeps the probe.
- Promoting a writable property into the known-property catalogue stays a manual code change.

### Type guessing

Runs in the browser only; the device serves raw strings.
1. Per value (trimmed), first match wins: `empty` (blank or only `-`), `date` (`DD.MM.YY`), `time` (`HH:MM[:SS]`), `decimal`, `padded int` (leading zero and more than one digit: `0100`, `01`, `00`), `int`, `text or hex` (only hex digits, at least one letter: `0A00`), `text`.
2. Per ID, across the whole history: all non-empty values agree → that type. All hex digits with at least one `text or hex` → `hex`. Any `text` → `text`. A mix of `int` and `padded int` → `padded int`. Any other mix → `text`. Unset values like `00.00.00` stay `date`.

### Browser storage

Decided in [What does the experiment panel look like in discover?](https://github.com/distante/esphome-components/issues/97), [How does a write probe run, and how does it fail safely?](https://github.com/distante/esphome-components/issues/98), [What do the exports record for experiments and write probes?](https://github.com/distante/esphome-components/issues/100) and [How do the decisions become the updated sniffer spec, and how is it sliced?](https://github.com/distante/esphome-components/issues/101).

Keyed by the ESP's MAC: a copy of the records, per-ID notes and mutes, the last discover range, the **experiments** (name, created time, full range, captures, probe results) and a **pending probe**. Notes, mutes and experiments never go to the device. The goal and the open experiment are not stored.
- **After a reboot** (`boot` changed): the device wins for every ID it has. IDs only in the browser copy stay, greyed out as stale, until the device sees them again.
- **Clear data** (a header button, after a confirm): `POST /sniffer/clear` empties the device's records, then the page drops its browser copy. Notes, mutes, experiments and the discover range stay. The device's scan and probe go on, but the page abandons a capture in flight and stores nothing ([Captures](#experiments)). If the device refuses or is offline, nothing is cleared. Other open pages see the new `boot` and take the empty store as a reboot, so their IDs turn stale.

**Checkboxes**, stored once per browser (not per MAC), and saved on every change:

| Stored | Default |
|---|---|
| "include muted IDs", "JSON: only the open experiment", "show known", "only candidates" | off |
| the type and status filter chips | all ticked |

- **"Reset options"**, a header button, restores the defaults.
- The search box, "changed within" and "colour by" are not stored.
- An unreadable stored checkbox falls back to its default on its own.
- Chips are stored as the unticked ones, so a new chip starts ticked.
- Other open pages pick up a change or a reset at once.
- With no experiment open, "only candidates" and "JSON: only the open experiment" show unticked and disabled; their stored value comes back when an experiment opens.

### Export

Decided in [What do sniffer exports record with two goals?](https://github.com/distante/esphome-components/issues/73), changed by [What do the exports record for experiments and write probes?](https://github.com/distante/esphome-components/issues/100): the mark is gone, experiments are added, and muted IDs are left out unless asked.

**The same in both goals:** a dump of every record in the browser copy, known properties included, **except muted IDs**. The page filters what you see; the export keeps the data. Everything is built by the page from browser storage.

- **"include muted IDs"** checkbox beside the export buttons, **off by default**, applying to the whole export. Off: muted IDs are left out of records (JSON and CSV) and capture entries. On: they are included there, with a `muted` flag per JSON record and a `muted` CSV column. Candidates and disqualified are derived with mutes applied either way, so a muted ID is never in them.
- **JSON**: the full record per ID (raw value, guessed type, registered flag, **`known`**, **`meaning`**, query status, change count, first seen and last changed as uptime ms and wall time, history with wall times, stale flag, note), plus device uptime, the `scan` object at export time (so `goal`, `from`, `to`, `ids`) and a top-level **`experiments`** array.
- **CSV**: one row per ID: `id, raw, guessed_type, registered, known, meaning, status, change_count, first_seen, last_changed, note` (plus `muted` when included). A header row plus one row per ID. Goal, range, experiments and write probes are JSON only; probe results reach the CSV through `note`.
- **`known`**: true/false, from the catalogue module.
- **`meaning`**: the expected meaning when a known property's value matches, `unexpected value x` when it doesn't (the same text as the check cards), and `null` (JSON) / empty (CSV) for unknown properties and for known ones with no value yet. History entries get no meanings.

**`experiments`**: every experiment stored for this MAC. A checkbox **"JSON: only the open experiment"** next to the export narrows the array to the open sub-tab; it is disabled when no experiment is open. Records stay the full dump either way. The CSV has no experiments, so the checkbox does not touch it.

- **Experiment** `{name, from, to, created_at, captures, candidates, disqualified, probes}`: `from`/`to` are the full range fixed at the first capture, `null` before any capture; `created_at` is a wall ISO time. Deleted captures are gone.
- **Capture** `{label, at, uptime_ms, boot, full, entries}`: `label` as typed; `at` wall ISO; `uptime_ms` and `boot` from the device at capture time, so a capture lines up with record history across a reboot; `full` is `true` for a full capture and `false` for a narrowed one; `entries` is `{"<id>": {"outcome": "ok"|"nak"|"timeout", "raw": "…"|null}}` for exactly the IDs the capture queried. An absent ID was not queried, which stays distinct from a timeout. Raw values use the records' `\u00XX` encoding.
- **Derived at export time** (there is no import, so nothing conflicts with a recompute): `candidates` `[{id, status: "candidate"|"corroborated", values: [{label, raw, count}]}]` with labels in their first spelling, and `disqualified` `[{id, conflicts: [{label, raws: [...]}]}]`.
- **`probes`**, oldest first: every probe and restore run from that experiment's cards, in the device's `probe` object shape (`kind, id, value, original, steps, verdict, restore`) plus a wall `at`. A failed restore is `restore: "failed"`, with the last `get` step's value and `original` giving "now Y, was Z". A pending probe the device no longer knew gets **`interrupted: true`**, `verdict: null` and its steps as last seen. A probe still running at export time is exported as is, with `active: true`.

**File names** ([#108](https://github.com/distante/esphome-components/issues/108)):

| Export | File name |
|---|---|
| CSV | `sec_touch_sniffer.csv` |
| JSON, full | `sec_touch_sniffer.json` |
| JSON, "JSON: only the open experiment" ticked | `sec_touch_sniffer-<slug>.json`; `sec_touch_sniffer.json` when the slug is empty |

The slug comes from the open experiment's name:

1. Unicode NFD, then combining marks (U+0300–U+036F) dropped: `ä` → `a`, `é` → `e`.
2. Lower-cased.
3. Each run of characters outside `a-z0-9` becomes one `-` (spaces, slashes, `ß`, non-Latin scripts).
4. Leading and trailing `-` dropped.

Example: `Kellerraum Schaltzeit 2 aktiv` → `sec_touch_sniffer-kellerraum-schaltzeit-2-aktiv.json`. Two experiments with the same name give the same file name; the browser adds its own `(1)`.

---

## Device/page contract

Decided in [What is the contract between the device and the page?](https://github.com/distante/esphome-components/issues/27), building on [the serving research](https://github.com/distante/esphome-components/issues/23), and for the goals in [How do the sniffer goal and the catalogue reach the device–page contract?](https://github.com/distante/esphome-components/issues/70) (resolution and amendment).

**Endpoints**, all under `/sniffer` because `web_server` owns `/`. The component implements `AsyncWebHandler` on `web_server_base` (like `prometheus`) and calls `base->init(); base->add_handler(this);` in `setup()`.

| Endpoint | Does |
|---|---|
| `GET /sniffer` | the page, gzipped, served from flash (no RAM copy). Every build bundles `web/` and the virtual catalogue module into one HTML file (`page_bundle.py`), gzips it and emits it as an `extern const uint8_t[]`, as `web_server` ships `js_include`. |
| `GET /sniffer/data?since=S` | JSON, see below |
| `POST /sniffer/scan/start?goal=G&from=F&to=T` or `?goal=discover&ids=L` | start a scan; empty body; 200 with the `scan` object, 400, or 409 when busy |
| `POST /sniffer/scan/stop` | stop the scan, either goal; empty body; 200 with the `scan` object |
| `POST /sniffer/clear` | empty every record, the history pool and `dropped`, and draw a new `boot`; empty body; 200 with `{}`. `seq` keeps going up. A running scan goes on, and so does a probe; the `probe` object stays. |
| `POST /sniffer/probe?id=X` | start a [write probe](#write-probe) of ID X with the request body as the value; 200 with the `probe` object, 400, or 409 when busy |
| `POST /sniffer/restore?id=X` | start a restore of ID X to the request body; 200 with the `probe` object, 400, or 409 when busy |

**Start parameters**, parsed in place like `since`:
- `goal` is `discover` or `check`. Missing or unknown means discover.
- `from`/`to` set the discover range. Both missing, with no `ids`, means 1–255. The device checks `0 ≤ from ≤ to ≤ 255` and replies **400** to a bad, partial or non-numeric range, without starting anything. Check ignores `from`/`to`.
- `ids` (discover only) is a comma-separated decimal list of IDs, for a narrowed capture: `ids=12,40,77`. A request carries either `ids` or `from`/`to`. The device holds the list as a set of IDs: it queries them in ascending order, and a duplicate counts once. The list is capped at **100 IDs**, so the URI stays under ESP-IDF's 512-byte `CONFIG_HTTPD_MAX_URI_LEN`. Known IDs in it are skipped, as in a range. A malformed list, an ID over 255, a list over the cap, or `ids` with `from`/`to` gets **400**. Check ignores `ids`. The set, ascending order and the 400 for `ids` with `from`/`to` were decided in [How do the decisions become the updated sniffer spec, and how is it sliced?](https://github.com/distante/esphome-components/issues/101).
- **Busy: 409.** Any start (scan, probe or restore) while one of them runs gets **409**, with the current `scan` and `probe` objects in the reply. The handler claims the run slot **atomically** when it accepts the request, so two tabs can't both get 200, and a 200 means the next finished run is the requester's own. This replaces the old idempotent 200 no-op. A stop during a scan works from any tab; a stop during a probe or restore, or while idle, is a 200 no-op.
- The handler only **queues** the action; the main loop applies it. The reply reflects the request, not the completion: its `active` is the requested state. The 4-byte lock-free `scan_request_` can't carry an ID list or a probe value, so **every start** (range scans included) goes through a mutex-guarded pending request that claims the run slot; it replaces `scan_request_` for starts.

**Probe and restore parameters.** `id` is the property ID; the body is the value's **exact bytes**, with `Content-Length` set. **400** when the value is over 15 bytes (`SetDataTask` holds 15 characters), holds NUL, TAB, STX or ETX (`send_set_message` frames the value with `%s`), or the ID is outside 0–255 or is a known property. The device checks only format and length. An empty value is allowed: an observed blank is a legitimate probe value.

**Transport: delta polling** (no SSE: the ESP32 `AsyncEventSource` is bound to `web_server` and a hand-rolled stream would hold a socket and the single httpd task).
- A global `seq` goes up on **any** record change, `last_seen` included.
- A delta returns every record with seq > `since`, **whole** (history included). The page replaces those records and never merges histories.
- `since=0` returns every record with `"full": true`.
- The page polls every 1 s, never overlapping. On error it backs off to 5 s with an offline badge, then returns to 1 s.
- The page resyncs with `since=0` on first load, when `boot` differs from the stored value (then runs the reboot merge), and when the server `seq` is lower than the page's `since`.

**Concurrency and RAM.** Handlers run on the httpd task while the main loop writes records.
- A mutex (ESPHome's `Mutex`, a FreeRTOS mutex on ESP32) guards the record store. The main loop and the httpd task's clear both write the records, so every read or write of them takes it, the main loop's reads included. The scan fields the handler reads (scanning flag, goal, range and position) are separate atomics, so a poll that races a scan step can show them out of step; the next poll corrects it.
- A mutex guards the `probe` object and the pending request. The handler copies `probe` under it, releases it, then formats the copy. The scan's ID list is read under the same mutex.
- Per record, the handler copies the record **in binary** (fields, raw value and its history entries, ≤ ~540 B) into static scratch under the lock, releases the lock, then formats JSON into a ~256 B chunk buffer and sends it with `httpd_resp_send_chunk` on the raw request. It sets status, content type and `Access-Control-Allow-Origin` itself and ends with the empty chunk.
- Peak RAM ~800 B, no heap growth per request, independent of escaping. The query string is parsed in place, not through the server's heap-allocated parameters.
- A response is not an atomic snapshot. A record changed mid-response is caught by the next poll through `seq`.

**JSON shape** (compact keys):

```json
{
  "mac": "aabbccddeeff", "name": "esp32-power-meter",
  "boot": "9f2c41ab", "uptime": 812000,
  "seq": 1204, "full": false, "dropped": 0,
  "scan": {"active": true, "goal": "discover", "from": 1, "to": 208, "ids": null, "next": 42, "done": false},
  "probe": null,
  "records": [
    {"id": 5, "cmd": 32, "raw": "0100",
     "first": 1000, "last": 5000, "changed": 3000,
     "changes": 1, "status": "ok", "status_at": 4000,
     "reg": false,
     "hist": [[3000, "0100"], [1000, "0000"]]}
  ]
}
```

- Times are uptime in ms (stored as seconds, sent as `s × 1000`).
- Unset timestamps and a never-seen raw value are `null`. A record that only ever failed has `cmd: null`, `raw: null` and `hist: []`.
- `status` is `unqueried`, `ok`, `nak` or `timeout`; `status_at` is `null` while `unqueried`.
- `hist` holds `[first seen, raw value]` pairs, newest first. The newest is the current value (`raw`, time `changed`); the rest come from the history pool.
- `reg` is read from the hub when served. It feeds only the badge on check's fan cards.
- `dropped` counts messages for IDs above 255.
- **`scan` is always present.**
  - `goal`: the running scan's goal, or the last scan's while idle; `"discover"` before any run.
  - `from`/`to`: the range of the running or last discover run; `null` when the running or last run is a check or a list run, or before any run.
  - `ids`: the ID list of the running or last list run; `null` for a range run, a check, or before any run. `goal` stays `discover`.
  - `next`: the ID the scan queries next; the last ID's while its query is in flight; while idle, the ID the last run stopped at, or `null` before any run and after a completed one.
  - `done`: true only while idle after the last run covered all of its IDs, until the next start.
  - They drive the progress labels.
- **`probe` is always present**, `null` before any probe or restore since boot:

  ```json
  "probe": {"active": false, "kind": "probe", "id": 77, "value": "03", "original": "01",
    "steps": [{"kind": "get", "outcome": "ok", "value": "01", "at": 812000},
              {"kind": "set", "outcome": "ok", "value": "03", "at": 812400}],
    "verdict": "writable", "restore": "ok"}
  ```

  - `kind`: `probe` or `restore`. A restore has `verdict: null` and only `restore`.
  - `value`: the value to write (a probe) or the requested original (a restore).
  - `original`: the original GET's answer, `null` until it answers; for a restore, the requested original.
  - `steps[].kind`: `get` or `set`; `outcome`: `ok`, `nak` or `timeout`; `value`: the GET's answer or the SET's value (`null` for a failed GET); `at` uptime ms.
  - `verdict`: `writable`, `not writable`, `unclear` or `nothing written`; `null` while the probe runs.
  - `restore`: `ok`, `failed` or `skipped` (no restore was needed, nothing written included); `null` until decided.
  - Raw values are encoded as in records.
- `serve_data_` reads `scan` and `probe` before the records, so a page that sees `done` also has the last answer.
- `since` missing or not a valid u32 counts as `0`, a full response.
- Responses carry `Cache-Control: no-store`.

**Raw value encoding:** byte-exact. Standard JSON escapes for `"` and `\`. Control bytes (< 0x20) and bytes ≥ 0x80 are sent as `\u00XX`, one code point per byte (Latin-1), so the page and the export recover the exact bytes. ASCII passes unchanged.

**Device identity:** browser storage is keyed by the ESP's **MAC** (12 lowercase hex digits), except the checkboxes ([Browser storage](#browser-storage)). `boot` is a random id generated at setup (8 hex digits), only for reboot detection; a clear draws a new one, since it empties the store as a reboot does. `name` is the ESPHome node name, for display. The panel's ID 3 is not used as a key.

---

## Hub integration

Decided in [How do raw strings and registered IDs reach the sniffer from the hub?](https://github.com/distante/esphome-components/issues/25).

1. **Message tap replaces the raw listener.** `RawMessageCallbackListener (command_id, property_id, int)` becomes a tap `(command_id, property_id, const std::string &raw_value)`, registered with e.g. `register_message_tap`. It fires for **every** data message in every mode, **before** the hub routes to entity listeners. In scan mode routing stops after the tap, as today. The sniffer is the only user.
2. **Scan outcome from the hub.** `get_last_scan_task_timed_out()` becomes `get_last_scan_outcome()` → `ok` / `nak` / `timeout`, set by `cleanup_after_task_complete` only while scan mode is active. A reply that arrives but can't be parsed counts as `timeout`, so the sniffer retries it once like a missing reply. `enter_scan_mode()` resets the outcome to `ok`, so a scan stopped during its retry wait can't hand a stale `timeout` to the next scan. The sniffer reads it in `on_queue_empty_`, so the queue-empty scan loop stays.
3. **`No listener for property_id` warning** fires only when no entity listener matched **and** no tap is registered.
4. **Registered flag** comes from `is_property_registered()` when data is served; the tap does not carry it.
5. **Unchecked SET.** `SetDataTask::create_unchecked(id, value)`, like `GetDataTask::create_unchecked`, and `add_discovery_set_task(id, value)` let the write probe SET an unknown ID. The value fits the existing 16-byte buffer. **It works only in scan mode:** outside it, `add_discovery_set_task` queues nothing, logs a warning and returns `false`, so outside scan mode nothing can write an unknown ID. In scan mode `cleanup_after_task_complete` records the SET's outcome in `get_last_scan_outcome()` (ACK = `ok`), and the sniffer's queue-empty loop drives the probe's steps from it. Decided in [How do capture scans and write probes reach the device–page contract?](https://github.com/distante/esphome-components/issues/99); the scan-mode guard in [How do the decisions become the updated sniffer spec, and how is it sliced?](https://github.com/distante/esphome-components/issues/101).
6. Independent of the multiple-listeners issue ([#21](https://github.com/distante/esphome-components/issues/21)): the tap is a list of observers, not keyed by ID.

---

## Implementation notes

### GET response protocol

The panel answers a GET with two separate messages:
1. `[STX][ACK][ETX]` confirms receipt.
2. `[STX]32[TAB]property_id[TAB]value[TAB]crc[ETX]` carries the data.

For unknown or unsupported IDs it sends `[STX][NAK][ETX]` instead of the data message (query status `nak`).

Some values are space-padded to a fixed width (`"00               "`), so a data message may arrive split across UART read cycles. `loop()` buffers partial messages and processes only once ETX arrives.

The task stays in `GET_DATA` after the ACK so the queue-empty callback (which drives scan advancement) can't fire before the data or NAK arrives.

### Scan timeout and retry

With no response within `TASK_TIMEOUT_MS` (2 s), the watchdog fires and the sniffer waits **5 s** before retrying that ID once. If the retry also times out, the query status becomes `timeout` and the scan advances immediately.

A write probe's timed-out SET (the probe's or the restore's) waits the same 5 s before its one retry.

The watchdog check runs regardless of whether the task queue is empty: tasks are popped when dispatched, so the queue is empty while waiting. The check must be unconditional in `loop()`.

Stopping the scan during the 5 s wait cancels the retry and resumes normal polling immediately.

### RAM only

Captured data lives in RAM and is lost on restart or flash. The page keeps a copy in browser storage and exports it. Persisting to flash/NVS is out of scope.

---

## Docs and migration

Ships as **v1.5.0** (minor: fans and HA entities are untouched; the hub gains the sniffer's tap and an unchecked SET).

**Changelog (v1.5.0):**
- **Breaking, YAML:** the `text_sensor` platform is gone and is a hard config error with a migration message. Use the top-level `sec_touch_sniffer:` block. `scan_switch`, `scan_start`/`scan_end` and `web_page` are removed; `web_server_base:` (or `web_server:`) is required.
- **Scans start only from the page**, which picks the goal (discover or check) and the discover range.
- **Discover skips every known property**, not only registered ones.
- **Scans no longer resume**: every start begins at the goal's first ID.
- **New check goal**: known properties with their expected meaning.
- **Experiments** in discover: captures at named values of one setting, candidates that track it, and **write probes** that test whether a corroborated candidate can be written, then restore it.
- **One run at a time:** a scan, probe or restore start while another runs gets **409**.
- **Export fields:** `known`, `meaning` and `experiments`; the JSON `scan` gains `goal` and `ids`. Muted IDs are left out unless "include muted IDs" is ticked.
- **Home Assistant:** the old sniffer text-sensor entity (and the scan switch, if used) is left orphaned. Delete it by hand.

**Readme** (sniffer section), rewritten around the goals and the `sec_touch_sniffer:` block:
- The two goals, the `/sniffer` URL and the `web_server_base` requirement.
- Memory cost: ~27 KB.
- Without `web_server: auth:`, the page, scan start/stop and **write probes** are **open on the LAN**: anyone on the network can write values to the panel's unknown properties. The readme advises `web_server: auth:` while the sniffer is enabled; it is not required.
- During captive-portal AP fallback, the portal claims every GET, so the page may be unreachable.
- Experiments and write probes, in a short how-to.
- Drops the text sensor, the scan switch, "scan skips registered IDs", the `text_sensor:` YAML examples, and the curl scan-start example (or updates it to `?goal=&from=&to=`, with 409 instead of "idempotent").

**`RAW_VALUES.md`**: the capture workflow uses the goals: check to confirm the known properties, discover with an experiment to find what a panel setting changes, and a write probe to test whether it can be written.

---

## Out of scope

- Persisting captured data to flash/NVS.
- A rich sniffer on ESP8266.
- Home Assistant integration of the page (cards, panels).
- Pass/fail validation in check, and decode rules beyond the catalogue's meaning tables.
- Storing the goal or the open experiment in the browser.
- Write probes for known properties or in the check goal: the component's own entities already write them.
- Generating catalogue entries (C++ snippets) from an experiment.
- An on-device test of how the panel answers a SET to a read-only property: no read-only property is known to test with.
- Importing an export back into the page.

---

## Acceptance criteria

1. In passive mode every data message is recorded, known and registered IDs included; existing controls are unaffected.
2. A discover start with `from`/`to` stops polling and listener updates and queries that range minus every known ID, from `from`; each queried record gets `ok`, `nak` or `timeout`. Without `from`/`to` or `ids` the range is 1–255.
3. A check start queries exactly the known IDs (1, 2, 48, 78–83, 173–178) in ascending order, registered ones included, and ignores `from`/`to`.
4. Stopping mid-scan resumes polling immediately; the next start begins at the goal's first ID. A completed scan resumes polling automatically and sets `done`.
5. A scan, probe or restore start while any of them runs is a 409 that starts nothing; two simultaneous starts never both get 200. A stop while idle, or during a probe or restore, is a 200 no-op. A bad, partial or non-numeric range, one above 255, a malformed, over-cap or over-255 `ids` list, or `ids` together with `from`/`to` is a 400 that starts nothing. `scan` is always present with `goal`, `from`, `to`, `ids`, `next` and `done` as specified.
6. A raw value change bumps `change_count`, `last_changed` and `seq`, and the previous value enters the history; a repeated identical message only updates `last_seen`.
7. An ID above 255 is dropped and counted. A bare `sec_touch_sniffer:` with `web_server_base:` is valid; without an HTTP server, on a non-ESP32 target, or as a second instance it is a config error. Any `text_sensor: - platform: sec_touch_sniffer` entry fails with the migration message.
8. The catalogue header is the only definition of the known IDs, level names, label names and `0A00`/`0800`; the component builds against it and the page's catalogue module is generated from it at build time.
9. Check shows one card per fan and a Global card with meaning and raw value; a non-matching value shows as `unexpected value x` in red; registered fans carry the badge.
10. Discover shows no known ID in the map (unless "show known" is ticked, then greyed and outside the filters), feed, filters or experiments. The mark is gone: no `mark.mjs`, no mark box, no "changed since mark" list, ring or filter, and no `mark`/`mark_raw` in the export.
11. JSON and CSV exports carry `known` and `meaning` per record and are identical in both goals; the JSON carries `scan.goal`, `scan.ids` and `experiments` with captures, candidates, disqualified and probes as specified. Muted IDs are absent from records and capture entries unless "include muted IDs" is ticked, then present and flagged `muted`; they are never candidates or disqualified. "JSON: only the open experiment" narrows `experiments` only, and names the JSON file after the open experiment ([File names](#export)).
12. A value with control or non-ASCII bytes round-trips byte-exact through JSON `\u00XX` escaping into the export, in records, capture entries and probe steps.
13. **Heap check**, on the maintainer's controller in a single boot: record heap free, largest block and min free before and after a discover scan that finds new IDs, a check scan, a narrowed capture and a write probe, with the page open plus a second web client, and through an OTA update. If min free drops under ~20 KB, shrink the history pool first.
14. A discover start with `ids` queries exactly the listed unknown IDs in ascending order, a duplicate once, and reports them in `scan.ids`. A capture is stored only when its run completes, with an entry per queried ID; a stopped run stores nothing, and a capture abandoned on a `boot` change stores nothing. Experiments survive Clear data.
15. Candidacy follows the walk, round-trip, consistency and corroboration rules and is recomputed from the stored captures, so deleting a capture can bring a disqualified ID back. A muted ID is never a candidate. A capture is full while the experiment has no candidates, when "full rescan" is pressed or with over 100 candidates, and otherwise queries only the current candidates.
16. A write probe of an unknown ID GETs the original, SETs the value (retrying a timed-out SET once), reads back for up to ~5 s and reports `writable`, `not writable`, `unclear` or `nothing written` with every step in `probe`. It restores unless the last read-back is the original, retries a failed restore once, and reports `restore: failed` with an ERROR log when that fails too. Regular polling is paused for the whole probe and resumes after it. Outside scan mode the hub's unchecked SET queues nothing.
17. `/sniffer/probe` and `/sniffer/restore` reject with 400 a value over 15 bytes or one that holds NUL, TAB, STX or ETX, and an ID outside 0–255 or known; a blank observed value is accepted. The page offers a write probe only on a corroborated candidate and only with values observed for it in the experiment's captures; the result is appended to the ID's note, and a probe the device no longer reports after a reboot shows as interrupted with a restore button. A 409 shows "busy: <what runs>"; while the device reports a run, the scan button is Stop during a scan or capture and disabled during a probe or restore, and Capture and every Write probe and restore button are disabled.
