# Sniffer / Listener Mode

## Goal

Capture what the SEC-Touch panel sends, **as raw strings**, so reverse-engineers can do two things, the two **sniffer goals**:

- **Discover** (the default): find and map **unknown properties**.
- **Check**: confirm the panel sends what the component expects for its **known properties**.

A light HTML page served by the ESP is the only interface: it starts scans, views, classifies, annotates and exports the data.

Background: v1.4.0 showed that property values are strings (`11.11.26`, `SWZ0021E21H`, `-50.0`, `0.360.00:00`), see [`RAW_VALUES.md`](../components/sec_touch/RAW_VALUES.md). The previous sniffer only saw `atoi` results.

Terms (property, raw value, property record, change, query status, known / registered / unknown property, sniffer goal, mark) are defined in [`GLOSSARY.md`](../GLOSSARY.md). Each section links the decision that holds its full reasoning: the [raw-value map](https://github.com/distante/esphome-components/issues/22) and the [sniffer-goals map](https://github.com/distante/esphome-components/issues/67).

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
- The sniffer queries the goal's IDs one at a time, from the goal's first ID. The next query is issued only after the previous answer (or timeout) is processed.
- In discover, known IDs are skipped. Their query status is left as it is.

**When stopped**, or when the run completes:
- Scanning stops immediately.
- Regular polling resumes at once, without waiting for the next poll cycle.

**No resume.** Every start begins at the goal's first ID; stop means abort. A full pass is the reverse-engineering loop (change the hardware, rescan, look at what changed since the [mark](#discover)), and a resumed scan would not give one. A discover run takes ~1–2 min (160 ms + round trip per answered ID); an unnarrowed 1–255 pass can take ~7 min longer when 208–255 never answer (~9 s per timed-out ID).

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

Decided in [What should the sniffer HTML page look like and do?](https://github.com/distante/esphome-components/issues/26) and, for the goals, [What does the sniffer page look like in check, and how does the user switch goals?](https://github.com/distante/esphome-components/issues/69). The prototypes live on the throwaway branches [`prototype/sniffer-page`](https://github.com/distante/esphome-components/blob/prototype/sniffer-page/components/sec_touch_sniffer/prototype/sniffer_page.prototype.html) and [`prototype/sniffer-goals`](https://github.com/distante/esphome-components/blob/prototype/sniffer-goals/components/sec_touch_sniffer/prototype/goals.prototype.html) (variant C in both). Audience: reverse-engineers. Function over polish, desktop only. On narrow screens the side column stacks below the grid; no further mobile work.

### Goal switch

- **Big page tabs** under the header, full width: "Discover — find unknown properties" and "Check — confirm known properties".
- The page **always opens in discover**. The goal is not stored in the browser.
- The scan button names the goal ("▶ Start discover scan"), and its label says what the scan queries.
- **Switching while a scan runs** asks for confirmation ("Switching stops the running scan"), then stops it. The user starts the new goal's scan by hand. The page never changes a running scan's goal.
- A scan the page did not start (another tab) is labelled with its own goal from `scan.goal`.

### Scan control

- Start/stop button with a progress bar and label: "Scanning ID n of a–b", "stopped at n", or "complete". Check counts its progress against the known IDs from the catalogue module.
- **Discover range:** a from/to input next to the start button, sent with the start. The page fills it with the last range used (browser storage, keyed by MAC), or 1–255 the first time. A 400 shows as an error next to the input.

### Check

**Panel-style cards**, no ID map, no filters, no change feed: 15 IDs fit on one screen.
- **One card per fan**: the level meaning in large type, the raw `id = value` under it, then the label meaning and its raw value. A `registered` badge marks fans this YAML uses (`reg`).
- **One Global card**: summer ventilation, date and clock, each with meaning and raw value.
- An unexpected value shows in red as `unexpected value x`. A known property with no value yet shows as not seen.

### Discover

Today's page minus known properties, plus the mark.
- **ID map**: a 16×16 grid of IDs 0–255 with **known IDs as blank gaps**. Each cell shows its ID and the start of its trimmed raw value, coloured by guessed type or by scan status (user's choice). A cell flashes on change. The scan cursor is outlined while a scan runs. A never-seen ID has a white cell with a dashed border, so it does not look like an `unqueried` one. The grid fills the width left of the side column and sizes its rows to the window height, so all 256 cells fit without scrolling.
- **Drawer** (click a cell): raw value (trimmed, with a marker when it was padded on the wire), type, query status, change count, last change, history (newest first) and a note field.
- **Change feed**: recent changes across unknown IDs (`time ID old → new`), newest first. Clicking an entry selects its ID.
- **Per-ID mute**: a drawer button removes the ID from the feed; its cell still updates.
- **Filters**: search (ID, value, note), **type chips** and **status chips** on one line (type │ status), changed recently. They fade and stripe non-matching cells and apply to the feed. "Hide registered" is gone: registered properties are known, so discover never shows them.
- **Mark** (browser only), in a box at the top of the side column, above the drawer and the list:
  - "Set mark" snapshots every raw value, with the wall time and device uptime.
  - An "only changed since mark (n)" filter dims every cell whose value equals the snapshot. Changed cells get an orange ring.
  - While the mark is set, the side column shows "changed since mark: ID mark value → now" above the change feed, which stays because the mark diff can't show a value toggled away and back.
  - "Move mark to now" and "Clear mark" sit with it.
  - The mark lives in page memory only; it is not stored in the browser and is lost on reload.

### Type guessing

Runs in the browser only; the device serves raw strings.
1. Per value (trimmed), first match wins: `empty` (blank or only `-`), `date` (`DD.MM.YY`), `time` (`HH:MM[:SS]`), `decimal`, `padded int` (leading zero and more than one digit: `0100`, `01`, `00`), `int`, `text or hex` (only hex digits, at least one letter: `0A00`), `text`.
2. Per ID, across the whole history: all non-empty values agree → that type. All hex digits with at least one `text or hex` → `hex`. Any `text` → `text`. A mix of `int` and `padded int` → `padded int`. Any other mix → `text`. Unset values like `00.00.00` stay `date`.

### Browser storage

Keyed by the ESP's MAC: a copy of the records, per-ID notes and mutes, and the last discover range. Notes and mutes never go to the device. The goal and the mark are not stored.
- **After a reboot** (`boot` changed): the device wins for every ID it has. IDs only in the browser copy stay, greyed out as stale, until the device sees them again.

### Export

Decided in [What do sniffer exports record with two goals?](https://github.com/distante/esphome-components/issues/73).

**The same in both goals:** a full dump of every record in the browser copy, known properties included. The page filters what you see; the export keeps the data.

- **JSON**: the full record per ID (raw value, guessed type, registered flag, **`known`**, **`meaning`**, query status, change count, first seen and last changed as uptime ms and wall time, history with wall times, stale flag, note, **`mark_raw`**), plus device uptime, the `scan` object at export time (so `goal`, `from`, `to`) and a top-level **`mark`**.
- **CSV**: one row per ID: `id, raw, guessed_type, registered, known, meaning, status, change_count, first_seen, last_changed, note, mark_raw`. A header row plus one row per ID; goal and range are JSON only.
- **`known`**: true/false, from the catalogue module.
- **`meaning`**: the expected meaning when a known property's value matches, `unexpected value x` when it doesn't (the same text as the check cards), and `null` (JSON) / empty (CSV) for unknown properties and for known ones with no value yet. History entries get no meanings.
- **`mark`**: `{set_at: <wall ISO>, uptime_ms}`, or `null` when no mark is set. **`mark_raw`** per record is its raw value at mark time, `null` (JSON) / empty (CSV) when the ID had no value then or no mark is set. This lets an LLM agent or a person diff before against after, which history (at most 8 values per ID, oldest dropped) can't promise for a noisy ID. The device still knows nothing of the mark.

---

## Device/page contract

Decided in [What is the contract between the device and the page?](https://github.com/distante/esphome-components/issues/27), building on [the serving research](https://github.com/distante/esphome-components/issues/23), and for the goals in [How do the sniffer goal and the catalogue reach the device–page contract?](https://github.com/distante/esphome-components/issues/70) (resolution and amendment).

**Endpoints**, all under `/sniffer` because `web_server` owns `/`. The component implements `AsyncWebHandler` on `web_server_base` (like `prometheus`) and calls `base->init(); base->add_handler(this);` in `setup()`.

| Endpoint | Does |
|---|---|
| `GET /sniffer` | the page, gzipped, served from flash (no RAM copy). Every build bundles `web/` and the virtual catalogue module into one HTML file (`page_bundle.py`), gzips it and emits it as an `extern const uint8_t[]`, as `web_server` ships `js_include`. |
| `GET /sniffer/data?since=S` | JSON, see below |
| `POST /sniffer/scan/start?goal=G&from=F&to=T` | start a scan; empty body; 200 with the `scan` object |
| `POST /sniffer/scan/stop` | stop the scan, either goal; empty body; 200 with the `scan` object |

**Start parameters**, parsed in place like `since`:
- `goal` is `discover` or `check`. Missing or unknown means discover.
- `from`/`to` set the discover range. Both missing means 1–255. The device checks `0 ≤ from ≤ to ≤ 255` and replies **400** to a bad, partial or non-numeric range, without starting anything. Check ignores `from`/`to`.
- **Idempotent.** A start while a scan runs is a no-op that returns 200, whatever its goal or range; the reply's `scan.goal` shows the running scan's goal. A stop while idle is a no-op that returns 200.
- The handler only **queues** the action (`request_scan()`, an atomic the component's `loop()` takes, now carrying goal and range); the main loop applies it. The reply reflects the request, not the completion: its `active` is the requested state.

**Transport: delta polling** (no SSE: the ESP32 `AsyncEventSource` is bound to `web_server` and a hand-rolled stream would hold a socket and the single httpd task).
- A global `seq` goes up on **any** record change, `last_seen` included.
- A delta returns every record with seq > `since`, **whole** (history included). The page replaces those records and never merges histories.
- `since=0` returns every record with `"full": true`.
- The page polls every 1 s, never overlapping. On error it backs off to 5 s with an offline badge, then returns to 1 s.
- The page resyncs with `since=0` on first load, when `boot` differs from the stored value (then runs the reboot merge), and when the server `seq` is lower than the page's `since`.

**Concurrency and RAM.** Handlers run on the httpd task while the main loop writes records.
- A mutex (ESPHome's `Mutex`, a FreeRTOS mutex on ESP32) guards the record store. Only the store's writes take it on the main loop; the main loop reads without it, being the only writer. The scan fields the handler reads (scanning flag, goal, range and position) are separate atomics, so a poll that races a scan step can show them out of step; the next poll corrects it.
- Per record, the handler copies the record **in binary** (fields, raw value and its history entries, ≤ ~540 B) into static scratch under the lock, releases the lock, then formats JSON into a ~256 B chunk buffer and sends it with `httpd_resp_send_chunk` on the raw request. It sets status, content type and `Access-Control-Allow-Origin` itself and ends with the empty chunk.
- Peak RAM ~800 B, no heap growth per request, independent of escaping. The query string is parsed in place, not through the server's heap-allocated parameters.
- A response is not an atomic snapshot. A record changed mid-response is caught by the next poll through `seq`.

**JSON shape** (compact keys):

```json
{
  "mac": "aabbccddeeff", "name": "esp32-power-meter",
  "boot": "9f2c41ab", "uptime": 812000,
  "seq": 1204, "full": false, "dropped": 0,
  "scan": {"active": true, "goal": "discover", "from": 1, "to": 208, "next": 42, "done": false},
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
  - `from`/`to`: the range of the running or last discover run; `null` when the running or last run is a check, or before any run.
  - `next`: the ID the scan queries next; the last ID's while its query is in flight; while idle, the ID the last run stopped at, or `null` before any run and after a completed one.
  - `done`: true only while idle after the last run covered all of its IDs, until the next start.
  - They drive the progress labels.
- `since` missing or not a valid u32 counts as `0`, a full response.
- Responses carry `Cache-Control: no-store`.

**Raw value encoding:** byte-exact. Standard JSON escapes for `"` and `\`. Control bytes (< 0x20) and bytes ≥ 0x80 are sent as `\u00XX`, one code point per byte (Latin-1), so the page and the export recover the exact bytes. ASCII passes unchanged.

**Device identity:** browser storage is keyed by the ESP's **MAC** (12 lowercase hex digits). `boot` is a random id generated at setup (8 hex digits), only for reboot detection. `name` is the ESPHome node name, for display. The panel's ID 3 is not used as a key.

---

## Hub integration

Decided in [How do raw strings and registered IDs reach the sniffer from the hub?](https://github.com/distante/esphome-components/issues/25).

1. **Message tap replaces the raw listener.** `RawMessageCallbackListener (command_id, property_id, int)` becomes a tap `(command_id, property_id, const std::string &raw_value)`, registered with e.g. `register_message_tap`. It fires for **every** data message in every mode, **before** the hub routes to entity listeners. In scan mode routing stops after the tap, as today. The sniffer is the only user.
2. **Scan outcome from the hub.** `get_last_scan_task_timed_out()` becomes `get_last_scan_outcome()` → `ok` / `nak` / `timeout`, set by `cleanup_after_task_complete` only while scan mode is active. A reply that arrives but can't be parsed counts as `timeout`, so the sniffer retries it once like a missing reply. `enter_scan_mode()` resets the outcome to `ok`, so a scan stopped during its retry wait can't hand a stale `timeout` to the next scan. The sniffer reads it in `on_queue_empty_`, so the queue-empty scan loop stays.
3. **`No listener for property_id` warning** fires only when no entity listener matched **and** no tap is registered.
4. **Registered flag** comes from `is_property_registered()` when data is served; the tap does not carry it.
5. Independent of the multiple-listeners issue ([#21](https://github.com/distante/esphome-components/issues/21)): the tap is a list of observers, not keyed by ID.

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

The watchdog check runs regardless of whether the task queue is empty: tasks are popped when dispatched, so the queue is empty while waiting. The check must be unconditional in `loop()`.

Stopping the scan during the 5 s wait cancels the retry and resumes normal polling immediately.

### RAM only

Captured data lives in RAM and is lost on restart or flash. The page keeps a copy in browser storage and exports it. Persisting to flash/NVS is out of scope.

---

## Docs and migration

Ships as **v1.5.0** (minor: the hub, fans and HA entities are untouched).

**Changelog (v1.5.0):**
- **Breaking, YAML:** the `text_sensor` platform is gone and is a hard config error with a migration message. Use the top-level `sec_touch_sniffer:` block. `scan_switch`, `scan_start`/`scan_end` and `web_page` are removed; `web_server_base:` (or `web_server:`) is required.
- **Scans start only from the page**, which picks the goal (discover or check) and the discover range.
- **Discover skips every known property**, not only registered ones.
- **Scans no longer resume**: every start begins at the goal's first ID.
- **New check goal**: known properties with their expected meaning.
- **Export fields:** `known`, `meaning`, `mark` and `mark_raw`; the JSON `scan` gains `goal`.
- **Home Assistant:** the old sniffer text-sensor entity (and the scan switch, if used) is left orphaned. Delete it by hand.

**Readme** (sniffer section), rewritten around the goals and the `sec_touch_sniffer:` block:
- The two goals, the `/sniffer` URL and the `web_server_base` requirement.
- Memory cost: ~27 KB.
- Without `web_server: auth:`, the page and scan start/stop are **open on the LAN**.
- During captive-portal AP fallback, the portal claims every GET, so the page may be unreachable.
- Drops the text sensor, the scan switch, "scan skips registered IDs", the `text_sensor:` YAML examples, and the curl scan-start example (or updates it to `?goal=&from=&to=`).

**`RAW_VALUES.md`**: the capture workflow uses the goals: check to confirm the known properties, discover with the mark to find what a panel setting changes.

---

## Out of scope

- Write probing (testing whether an ID is writable): changes device state.
- Persisting captured data to flash/NVS.
- A rich sniffer on ESP8266.
- Home Assistant integration of the page (cards, panels).
- Pass/fail validation in check, and decode rules beyond the catalogue's meaning tables.
- Storing the goal or the mark in the browser.

---

## Acceptance criteria

1. In passive mode every data message is recorded, known and registered IDs included; existing controls are unaffected.
2. A discover start with `from`/`to` stops polling and listener updates and queries that range minus every known ID, from `from`; each queried record gets `ok`, `nak` or `timeout`. Without `from`/`to` the range is 1–255.
3. A check start queries exactly the known IDs (1, 2, 48, 78–83, 173–178) in ascending order, registered ones included, and ignores `from`/`to`.
4. Stopping mid-scan resumes polling immediately; the next start begins at the goal's first ID. A completed scan resumes polling automatically and sets `done`.
5. A start while a scan runs is a 200 no-op whose `scan.goal` is the running goal; a stop while idle is a 200 no-op; a bad, partial or non-numeric range, or one above 255, is a 400 that starts nothing. `scan` is always present with `goal`, `from`, `to`, `next` and `done` as specified.
6. A raw value change bumps `change_count`, `last_changed` and `seq`, and the previous value enters the history; a repeated identical message only updates `last_seen`.
7. An ID above 255 is dropped and counted. A bare `sec_touch_sniffer:` with `web_server_base:` is valid; without an HTTP server, on a non-ESP32 target, or as a second instance it is a config error. Any `text_sensor: - platform: sec_touch_sniffer` entry fails with the migration message.
8. The catalogue header is the only definition of the known IDs, level names, label names and `0A00`/`0800`; the component builds against it and the page's catalogue module is generated from it at build time.
9. Check shows one card per fan and a Global card with meaning and raw value; a non-matching value shows as `unexpected value x` in red; registered fans carry the badge.
10. Discover shows no known ID in the map, feed or filters; setting the mark and changing values shows exactly the changed IDs under "only changed since mark".
11. JSON and CSV exports carry `known`, `meaning` and `mark_raw` per record and are identical in both goals; the JSON carries `mark` and `scan.goal`.
12. A value with control or non-ASCII bytes round-trips byte-exact through JSON `\u00XX` escaping into the export.
13. **Heap check**, on the maintainer's controller in a single boot: record heap free, largest block and min free before and after a discover scan that finds new IDs and a check scan, with the page open plus a second web client, and through an OTA update. If min free drops under ~20 KB, shrink the history pool first.
