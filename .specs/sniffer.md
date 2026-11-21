# Sniffer / Listener Mode

## Goal

Capture what the SEC-Touch panel sends, **as raw strings**, so unknown properties can be identified and mapped. A light HTML page served by the ESP lets reverse-engineers view, classify, annotate and export that data.

Background: v1.4.0 showed that property values are strings (`11.11.26`, `SWZ0021E21H`, `-50.0`, `0.360.00:00`), see [`RAW_VALUES.md`](../components/sec_touch/RAW_VALUES.md). The previous sniffer only saw `atoi` results.

Terms (property, raw value, property record, change, query status) are defined in [`GLOSSARY.md`](../GLOSSARY.md). Each section links the decision that holds its full reasoning ([wayfinder map](https://github.com/distante/esphome-components/issues/22)).

**Platform: ESP32 only.** The whole `sec_touch_sniffer` platform fails config validation on any other target. There is no fallback to the old int sniffer.

---

## Behaviour

### Passive capture

The sniffer observes **every** data message, registered property IDs included. Registered IDs are recorded read-only and flagged as registered. Registered components (e.g. fan controls) keep working normally.

Regular polling keeps running in passive mode, so registered IDs are seen through it.

### Active scan mode

An optional mode toggled from the switch or the page. Both drive the same scan state and each reflects scans started from the other.

**When turned on:**
- Regular polling stops completely. Tasks already queued (e.g. pending fan SET commands) are discarded.
- Registered component listeners stop receiving updates.
- The sniffer queries the configured ID range one at a time. The next query is issued only after the previous answer (or timeout) is processed.
- Registered IDs are skipped. They are not queried and their query status stays `unqueried`.

**When turned off:**
- Scanning stops immediately. The last queried ID is saved.
- Regular polling resumes at once, without waiting for the next poll cycle.

**Resume:** turning scan mode back on continues from the saved position. After a completed range, the next run restarts from the beginning.

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
- **Query status** is set only by the sniffer's own scan queries. Passive messages never touch it. `timeout` means no answer after the retry; `nak` means the panel refused the ID.
- A **failure** updates status and `status_at` only. The last good value, history and `change_count` stay, and failures never enter the history.
- A record that only ever failed has no raw value, empty history, and unset `first_seen` / `last_seen` / `last_changed`.
- **Timestamps** are device uptime. No RTC is needed: the page renders wall time as `browser_now - (uptime_now - t)`. The sniffer no longer uses `time:`.

---

## Memory caps

Decided in [What are the sniffer's memory caps?](https://github.com/distante/esphome-components/issues/31), sized against [measured heap](https://github.com/distante/esphome-components/issues/30#issuecomment-6911212958) on a real controller (BLE proxy, `web_server`, ESP-IDF 5.5.6): ~57 KB min free, ~31 KB largest free block.

**Budget:** ~25 KB total, ~27 KB with the page (the ~20 KB first decided grew with the per-record `seq` and the 59 B raw cap), allocated once in `setup()`. No per-ID allocations and no single block over 16 KB. All caps are named constants in the header, not YAML options.

| What | Cap | Cost |
|---|---|---|
| Property IDs | 256 slots, indexed by ID 0–255 | — |
| Record fields | 4 × u32 uptime **seconds**, u32 `change_count`, u32 `seq` (for the page's deltas), cmd, status, raw length, flags (32 B) | 256 × 32 B = 8 KB (one block) |
| Raw value | 59 B, never truncated | 256 × 59 B ≈ 15 KB (one block) |
| History | 2 KB pool, depth 8 per ID (current value included) | 2 KB, **only with `web_page: true`** |
| Serving scratch | binary record copy ≤ ~540 B + 256 B JSON chunk | ~800 B, static |
| Text sensor state | 2048 chars | ≤ 2 KB heap string |

- **IDs above 255** are dropped, counted (`dropped` in the data JSON) and logged once. `scan_end > 255` is a config error.
- **Timestamps** are stored as u32 uptime seconds (good for 136 years) and sent to the page as ms (`s × 1000`).
- **Raw cap 59 B** is everything the hub's 64 B frame buffer (`IncomingMessage::buffer`, `FRAME_BUFFER_SIZE`) can deliver: the hub routes a frame with empty command and property IDs and no CRC, so only STX, two TABs and ETX surround the value. The cap is derived from `FRAME_BUFFER_SIZE`, the constant the buffer is declared with, so they can't drift. The longest raw value seen so far is 17 B.
- **History pool:** one 2 KB array of variable-length entries `{id, len, t, bytes}` (~12 B typical, ~170 entries). It holds **previous values only**; the current value lives in the record and its time is `last_changed`.
  - An ID keeps at most 7 previous values. Inserting another drops that ID's oldest.
  - When the pool is full, drop the oldest entry of the ID with the most entries, so a fast-changing ID (the clock, ID 2) can't push out the history of rare IDs.
  - Each insert costs one memmove of ≤ 2 KB.
  - With `web_page: false` there is no pool and no history; everything else still updates.

---

## Outputs

### Text sensor

Publishes `id=raw` pairs, comma-separated (`1=11.11.26, 3=SWZ0021E21H, 5=0100`), for every **unregistered** record that **has a raw value** (passive captures and scan `ok` answers). Records that only ever failed and registered IDs are left out; the page shows those.

- **During an active scan** the sensor is not updated. One publish happens when the scan ends (completed or stopped).
- **In passive mode** it is updated on every new or changed record that it lists.
- Messages that change nothing never trigger a publish.
- Capped at 2048 chars (`MAX_STATE_LEN`). When cut, the state ends with `, …+N more`. The state is built into a `std::string` reserved once, not a buffer on the loop task stack. A full scan measured ~7 chars per ID (1392 chars for 196 IDs).
- The command id and timestamps are not in the state but are logged at `[I]` for every new record.

### Switch (optional)

Reflects whether scan mode is active and can be toggled from Home Assistant.

### HTML page (optional, `web_page: true`)

See [Page](#page) and [Device/page contract](#devicepage-contract).

---

## Configuration

Decided in [How is the sniffer page configured in YAML?](https://github.com/distante/esphome-components/issues/29).

```yaml
web_server_base:   # or web_server:, captive_portal:, prometheus:

text_sensor:
  - platform: sec_touch_sniffer
    name: Sniffer
    scan_start: 1
    scan_end: 208      # optional; 0..255
    scan_switch:       # optional
      name: Sniffer Scan
    web_page: true     # optional, default false
```

- **`web_page`**: a boolean, default off. It can widen to a mapping later without breaking `true`.
- **Scan range**: `scan_start` ≤ `scan_end` ≤ 255, else a config error. Without `scan_end`, active scan is disabled; the page then shows passive data without scan controls.
- **`time_id`**: removed. Setting it is a hard config error with a hint that timestamps are uptime-based and the page shows wall time.
- **ESP32 only**: any other target is a config error.
- **HTTP server**: `web_server_base` is **not** auto-loaded, so page-off builds carry no server code. `web_page: true` without `web_server_base` loaded fails with "add `web_server_base:` (or `web_server:`)".
- **Auth and port** are inherited. The handler registers with `add_handler()`, so `web_server: auth:` protects it when set. The port is `web_server_base`'s. The sniffer has no auth or port options.
- **Sockets**: with `web_page: true`, `web_server_base.add_httpd_clients(2, "sec_touch_sniffer")`: one for the polling tab, one spare for a reload or keep-alive. It calls `socket.consume_sockets(2, "sec_touch_sniffer")` and also raises the httpd client limit (`max_open_sockets`) to match, as `web_server` does for its own clients.

---

## Page

Decided in [What should the sniffer HTML page look like and do?](https://github.com/distante/esphome-components/issues/26). The prototype lives on the throwaway branch [`prototype/sniffer-page`](https://github.com/distante/esphome-components/blob/prototype/sniffer-page/components/sec_touch_sniffer/prototype/sniffer_page.prototype.html) (variant C). Audience: reverse-engineers. Opt-in, function over polish, desktop only.

**Layout:**
- **ID map**: a 16×16 grid of IDs 0–255. Each cell shows its ID and the start of its trimmed raw value, coloured by guessed type or by scan status (user's choice). Registered IDs have a heavy border. A cell flashes on change. The scan cursor is outlined while a scan runs.
- **Drawer** (click a cell): raw value (trimmed, with a marker when it was padded on the wire), type, registered flag, query status, change count, last change, history (newest first) and a note field.
- **Change feed**: recent changes across IDs (`time ID old → new`), newest first. Clicking an entry selects its ID.
- **Per-ID mute**: a drawer button removes the ID from the feed; its cell still updates. Exists because the polled clock (ID 2) would flood the feed.
- **Filters**: search (ID, value, note), type, query status, hide registered, changed recently. They dim non-matching cells and apply to the feed.
- **Scan control**: start/stop button with a progress bar and label: "Scanning ID n of a–b", "stopped at n, resumes there", or "complete". The range is read-only.
- On narrow screens the side column stacks below the grid; no further mobile work.

**Type guessing** runs in the browser only; the device serves raw strings.
1. Per value (trimmed), first match wins: `empty` (blank or only `-`), `date` (`DD.MM.YY`), `time` (`HH:MM[:SS]`), `decimal`, `padded int` (leading zero and more than one digit: `0100`, `01`, `00`), `int`, `text or hex` (only hex digits, at least one letter: `0A00`), `text`.
2. Per ID, across the whole history: all non-empty values agree → that type. All hex digits with at least one `text or hex` → `hex`. Any `text` → `text`. A mix of `int` and `padded int` → `padded int`. Any other mix → `text`. Unset values like `00.00.00` stay `date`.

**Browser storage**, keyed by the ESP's MAC: a copy of the records, per-ID notes and mutes. Notes and mutes never go to the device.
- **After a reboot** (`boot` changed): the device wins for every ID it has. IDs only in the browser copy stay, greyed out as stale, until the device sees them again.

**Export**, both including notes:
- **JSON**: the full record per ID (raw value, guessed type, registered flag, query status, change count, first seen and last changed as uptime ms and wall time, history with wall times, stale flag, note), plus device uptime and scan state at export time.
- **CSV**: one row per ID: `id, raw, guessed_type, registered, status, change_count, first_seen, last_changed, note`.

---

## Device/page contract

Decided in [What is the contract between the device and the page?](https://github.com/distante/esphome-components/issues/27), building on [the serving research](https://github.com/distante/esphome-components/issues/23).

**Endpoints**, all under `/sniffer` because `web_server` owns `/`. The component implements `AsyncWebHandler` on `web_server_base` (like `prometheus`) and calls `base->init(); base->add_handler(this);` in `setup()`.

| Endpoint | Does |
|---|---|
| `GET /sniffer` | the page, gzipped, served from flash (`constexpr uint8_t[]`, no RAM copy). Until the page lands ([#35](https://github.com/distante/esphome-components/issues/35)), a plain placeholder from flash that links to the data. |
| `GET /sniffer/data?since=S` | JSON, see below |
| `POST /sniffer/scan/start` | start scan; empty body; 200 with the `scan` object |
| `POST /sniffer/scan/stop` | stop scan; empty body; 200 with the `scan` object |

- **Start/stop are idempotent.** A start during a scan or a stop while idle is a no-op that returns 200.
- The handler only **queues** the action (`request_scan()`, an atomic the component's `loop()` takes); the main loop applies it through the same path as the switch, so the switch moves too. The reply reflects the request, not the completion: its `active` is the requested state.
- Without a scan range, `scan` is `null` and the page hides the controls. Start and stop then do nothing and reply 200 with `null`.

**Transport: delta polling** (no SSE: the ESP32 `AsyncEventSource` is bound to `web_server` and a hand-rolled stream would hold a socket and the single httpd task).
- A global `seq` goes up on **any** record change, `last_seen` included.
- A delta returns every record with seq > `since`, **whole** (history included). The page replaces those records and never merges histories.
- `since=0` returns every record with `"full": true`.
- The page polls every 1 s, never overlapping. On error it backs off to 5 s with an offline badge, then returns to 1 s.
- The page resyncs with `since=0` on first load, when `boot` differs from the stored value (then runs the reboot merge), and when the server `seq` is lower than the page's `since`.

**Concurrency and RAM.** Handlers run on the httpd task while the main loop writes records.
- A mutex (ESPHome's `Mutex`, a FreeRTOS mutex on ESP32) guards the record store. Only the store's writes take it on the main loop; the main loop reads without it, being the only writer. The scan fields the handler reads (scanning flag and position) are separate atomics, so a poll that races a scan step can show them out of step; the next poll corrects it.
- Per record, the handler copies the record **in binary** (fields, raw value and its history entries, ≤ ~512 B) into static scratch under the lock, releases the lock, then formats JSON into a ~256 B chunk buffer and sends it with `httpd_resp_send_chunk` on the raw request. It sets status, content type and `Access-Control-Allow-Origin` itself and ends with the empty chunk.
- Peak RAM ~800 B, no heap growth per request, independent of escaping. The query string is parsed in place, not through the server's heap-allocated parameters.
- A response is not an atomic snapshot. A record changed mid-response is caught by the next poll through `seq`.

**JSON shape** (compact keys):

```json
{
  "mac": "aabbccddeeff", "name": "esp32-power-meter",
  "boot": "9f2c41ab", "uptime": 812000,
  "seq": 1204, "full": false, "dropped": 0,
  "scan": {"active": true, "from": 1, "to": 255, "next": 42, "done": false},
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
- `hist` holds `[first seen, raw value]` pairs, newest first. The newest is the current value (`raw`, time `changed`); the rest come from the history pool. Empty when the pool is off.
- `reg` is read from the hub when served.
- `dropped` counts messages for IDs above 255.
- `scan` is `null` without a scan range.
- `scan.next` is the ID the scan queries next, or resumes at after a stop; `to` while the last query of a run is in flight; `from` before the first run and after a completed one. `scan.done` is true only while idle after a run covered the whole range, until the next start. They drive the progress labels.
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

### Text sensor and MQTT

A full scan produces ~1.4 KB of state. With MQTT instead of the native API, the broker's `max_packet_size` must be at least 4096 bytes; Mosquitto's default silently drops larger publishes and HA shows "unknown". The native API has no such limit.

### RAM only

Captured data lives in RAM and is lost on restart or flash. The page keeps a copy in browser storage and exports it. Persisting to flash/NVS is out of scope.

### Stale code comments

`sec_touch_sniffer.h` says ~35 chars per text sensor entry and 80–100 B per `std::map` entry. Both are wrong (measured ~7 chars and 52 B) and go away with this rewrite.

---

## Docs and migration

**Breaking changes** (changelog):
- Text sensor format: `id=raw` instead of `id=int`, and the listed records changed (see [Text sensor](#text-sensor)).
- `time_id` removed (hard config error).
- The sniffer is ESP32-only.
- `scan_end` is capped at 255.
- `web_page: true` needs `web_server_base` (or `web_server:` etc.).

**Readme** (sniffer section):
- The `web_page` option, the `/sniffer` URL and the `web_server_base` requirement.
- Memory cost: ~25 KB, ~27 KB with the page.
- Without `web_server: auth:`, the page and scan start/stop are **open on the LAN**.
- During captive-portal AP fallback, the portal claims every GET, so the page may be unreachable.

**`RAW_VALUES.md`**: rewrite the capture workflow around the page and its export instead of DEBUG logs.

---

## Out of scope

- Write probing (testing whether an ID is writable): changes device state.
- Persisting captured data to flash/NVS.
- A rich sniffer on ESP8266.
- Home Assistant integration of the page (cards, panels).

---

## Acceptance criteria

1. In passive mode, unregistered IDs with a raw value appear in the text sensor as `id=raw`, byte-exact; registered IDs are recorded and flagged on the page but not in the text sensor; existing controls are unaffected.
2. Turning scan mode on stops polling and listener updates; the scan queries the range, skipping registered IDs; each queried record gets `ok`, `nak` or `timeout`.
3. Turning scan mode off mid-scan resumes polling immediately; the next start resumes from the saved position.
4. A completed scan resumes polling automatically, the switch shows off, and the text sensor publishes once.
5. A raw value change bumps `change_count`, `last_changed` and `seq`, and the previous value enters the history (page on); a repeated identical message only updates `last_seen`.
6. An ID above 255 is dropped and counted; `scan_end: 256` is a config error; `time_id`, a non-ESP32 target, and `web_page: true` without `web_server_base` are config errors.
7. With `web_page: true`, `GET /sniffer` serves the page, `GET /sniffer/data?since=0` returns all records with `"full": true`, and a delta returns only records changed since `S`. Start/stop are idempotent and also move the switch.
8. A value with control or non-ASCII bytes round-trips byte-exact through JSON `\u00XX` escaping into the export.
9. The text sensor never exceeds 2048 chars and ends with `, …+N more` when cut.
10. **Heap check**, on the maintainer's controller in a single boot: record heap free, largest block and min free before and after a scan that finds new IDs, with the page open plus a second web client, and through an OTA update. If min free drops under ~20 KB, shrink the history pool first.
