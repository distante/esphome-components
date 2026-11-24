# Research: ESPHome entities for a time program

Answers [#112](https://github.com/distante/esphome-components/issues/112) for the map [#111](https://github.com/distante/esphome-components/issues/111). The block layout is in the sec_touch readme, "[Time programs](../../components/sec_touch/readme.md#time-programs-zeitprogramm-ids-101166)".

Sources: the ESPHome source vendored at `esphome/` (2026.11.0; paths below are relative to `esphome/esphome/`), esphome.io, the ESPHome and Home Assistant GitHub repos, and the HA developer docs. "Estimate" marks the one claim not read from a source.

## Summary

| Field per switch time | Entity | Fit |
|---|---|---|
| Time (`HH:MM:SS`) | `datetime`, `type: time` | Good. HA shows HH:MM and keeps the seconds. |
| Modus (`0`–`9`) | `select`, 10 options | Good. Control by index. |
| Aktiv (one bit of the shared mask) | `switch` | Good. 5 switches share one ID, so a write is read-modify-write. |
| Grouping per fan pair | sub-device (`esphome: devices:` + `device_id`) | Good. One HA device per pair. |

Per pair: 5 × 3 = 15 entities. For 6 pairs: 90.

## Minimum ESPHome version

The repo declares no minimum. CI installs the vendored submodule, pinned at ESPHome **2026.11.0** ([`requirements_test.txt`](../../requirements_test.txt), [`tests.yml`](../../.github/workflows/tests.yml), `const.py` `__version__`). So 2026.11.0 is the only tested version.

| Feature needed | Since | Source |
|---|---|---|
| `datetime` `type: time` | 2024.4.0 | [esphome#6399](https://github.com/esphome/esphome/pull/6399) "Add support for time entities", in the 2024.4.0 release notes |
| Sub-devices and areas | 2025.7.0 | [esphome#8544](https://github.com/esphome/esphome/pull/8544), [2025.7.0 changelog](https://esphome.io/changelog/2025.7.0/) |
| Required `relation:` on each sub-device | 2026.11.0 (present in the vendored tree) | [esphome#21232](https://github.com/esphome/esphome/pull/21232), merged 2026-10-22, breaking |

Caveat: a `devices:` entry with `relation:` fails on ESPHome before that change; one without it fails on 2026.11.0.

## `datetime` with `type: time`

| Topic | Finding | Source |
|---|---|---|
| Base class | `datetime::TimeEntity`; pure virtual `control(const TimeCall &call)` | `components/datetime/time_entity.h` |
| Schema / codegen | `datetime.time_schema(Class)`, then `datetime.register_datetime(var, config)` or `new_datetime` | `components/datetime/__init__.py` |
| State | Protected `hour_`, `minute_`, `second_`; set them, then `publish_state()` | `time_entity.h`, `time_entity.cpp` |
| Range check | `publish_state()` rejects hour > 23, minute or second > 59: logs an error, sets "no state" | `time_entity.cpp` |
| Write path | HA → `TimeCommandRequest{hour, minute, second}` → `TimeCall::perform()` → `validate_()` (drops out-of-range parts) → `control(call)`. `call.get_hour()` etc. return `optional<uint8_t>` | `time_entity.cpp`, `components/api/api.proto` |
| No state | `TimeStateResponse.missing_state` = `!has_state()`; HA shows `unknown` | `api.proto`, [HA esphome `time.py`](https://github.com/home-assistant/core/blob/dev/homeassistant/components/esphome/time.py) |
| String parse | `TimeCall::set_time(const char*, len)` parses `HH:MM:SS` via `ESPTime::strptime` | `time_entity.cpp` |
| `time:` clock | Needed only for `on_time`; the entity itself needs none | `datetime/__init__.py` `_validate_time_present` |
| HA requirement | Home Assistant 2024.4 or newer | [esphome.io datetime](https://esphome.io/components/datetime/) |

**Seconds.** The API and HA carry seconds end to end: HA's `native_value` is a `datetime.time`, and the state reads like `07:30:00` ([HA time entity](https://contributors.home-assistant.io/core/entities/time/), [esphome `time.py`](https://github.com/home-assistant/core/blob/dev/homeassistant/components/esphome/time.py)). HA's UI hides them: the more-info dialog and the entity row use `ha-time-input` without `enable-second` ([more-info-time.ts](https://github.com/home-assistant/frontend/blob/dev/src/dialogs/more-info/controls/more-info-time.ts), [hui-time-entity-row.ts](https://github.com/home-assistant/frontend/blob/dev/src/panels/lovelace/entity-rows/hui-time-entity-row.ts)). The input passes the current seconds through ([ha-time-input.ts](https://github.com/home-assistant/frontend/blob/dev/src/components/ha-time-input.ts), [ha-base-time-input.ts](https://github.com/home-assistant/frontend/blob/dev/src/components/ha-base-time-input.ts)), so a UI edit sends `HH:MM:<current seconds>`. The `time.set_value` action takes `HH:MM:SS`.

Implication: the component can pass the panel's `HH:MM:SS` through unchanged. Whether the panel takes non-zero seconds is a device question.

## `select` for Modus

| Topic | Finding | Source |
|---|---|---|
| Codegen | `select.new_select(config, options=[...])` / `register_select` | `components/select/__init__.py` |
| Option storage | Options go into a PROGMEM array emitted once per distinct list (`cg.shared_progmem_array`): 30 Modus selects share one table | `select/__init__.py`, `cpp_generator.py` |
| Control | Override `control(size_t index)` (preferred, no string work) or `control(const std::string&)` | `components/select/select.h` |
| Publish | `publish_state(size_t index)` | `select.h` |

Modus value = option index: `0`–`6` Manuell level, `7` Stoßlüften, `8` Feuchte, `9` CO2. The repo's `SecTouchModeSelect` already follows this pattern (`components/sec_touch/select/`).

Gotcha: a device value outside 0–9 has no option. The component must decide what to publish then (for example, no state).

## `switch` for Aktiv

| Topic | Finding | Source |
|---|---|---|
| Control | Override `write_state(bool)`; call `publish_state(bool)` once the device confirms | `components/switch/switch.cpp` |
| Default restore | `ALWAYS_OFF`: nothing stored in flash | `switch/__init__.py` `switch_schema` |
| Flash wear | With a `RESTORE_*` mode, every `publish_state` saves to preferences | `switch.cpp` `publish_state` |
| Boot write | `restore()` writes an initial state only when the component calls it; the repo's summer switch does not | `switch.cpp`, `components/sec_touch/switch/` |

Gotchas:

- All 5 Aktiv flags live in one ID. A write reads the current mask, flips one bit and writes the mask. Two quick toggles must not race: queue or merge them.
- Keep the default restore mode and never call `restore()`, so a boot never writes to the panel.

## Grouping per fan pair

| Option | What HA shows | Source |
|---|---|---|
| Sub-device per pair (`esphome: devices:` + `device_id:` on each entity) | One HA device per pair, linked to the ESP node | [esphome.io core config](https://esphome.io/components/esphome/), [2025.7.0 blog](https://esphome.io/blog/2025/07/16/esphome-2025-7/) |
| `relation: child` | The sub-device is a logical part of this node ("one zone of a controller"). `via` = a separate physical device behind the node. Older HA treats every sub-device as `via` | [esphome.io core config](https://esphome.io/components/esphome/), [esphome#21232](https://github.com/esphome/esphome/pull/21232) |
| `areas:` + `area_id` on a device | Puts that pair's device in an HA area (the room the pair serves) | [esphome.io core config](https://esphome.io/components/esphome/) |
| Name prefix only (`Pair 1 Schaltzeit 1 Zeit`) | All 90 entities on the one node device | — |

From the source:

- `device_id` is in `ENTITY_BASE_SCHEMA`, so every entity built from `select_schema`, `switch_schema` or `time_schema` takes it with no extra code (`config_validation.py`).
- Duplicate-name checks are per device: the same name (`Schaltzeit 1 Zeit`) may repeat on each pair's sub-device (`core/entity_helpers.py` `entity_duplicate_validator`).
- Every list and state message carries `device_id` (`api.proto`, under `USE_DEVICES`).
- Device and area lists are `StaticVector`s sized at compile time (`core/application.h`).
- `entity_category: config` fits settings like these; it is a standard entity option (`ENTITY_BASE_SCHEMA`). HA lists such entities under "Configuration" on the device page.

## Alternatives

| Option | Read | Write | Fit |
|---|---|---|---|
| `api: actions:` custom action | No state. With `supports_response: optional` or `only` it returns JSON per call | `esphome.<node>_<action>` with `bool`, `int`, `float`, `string` and their arrays (`int[]`) | Good as an extra bulk setter ("whole program of pair N"), not as the only interface |
| `text` entity | One string | One string | Poor UX: the user edits a coded string (max 255 chars by default), no per-field checks in HA |
| HA schedule helper | HA-side only | HA-side only | Does not map: weekly on/off blocks with start and end, optional per-block `data`, no device backing |

Sources: [esphome.io api, User-defined Actions](https://esphome.io/components/api/#api-device-actions), `components/text/__init__.py` (`max_length` default 255), [HA schedule](https://www.home-assistant.io/integrations/schedule/).

The panel's switch times are points (from this time, run Modus X), not on/off blocks. A schedule helper could reach the panel only through an automation calling a custom action, one way: panel edits would not flow back.

## Cost of ~90 extra entities

| Item | Finding | Source |
|---|---|---|
| Hard entity limit | None found. Entity lists are compile-time `StaticVector`s sized by `ESPHOME_ENTITY_<PLATFORM>_COUNT` | `core/config.py` `_add_platform_defines`, `core/application.h` |
| Per-entity base | `EntityBase`: name pointer, 32-bit hash, `Device*`, 1 flag byte + 3 index bytes | `core/entity_base.h`; packed by [esphome#9076](https://github.com/esphome/esphome/pull/9076) |
| Select option text | In flash, shared | [select](#select-for-modus) |
| Switch | + preferences handle, dedup state, `inverted_` | `components/switch/switch.h` |
| Time | + 3 bytes | `time_entity.h` |
| `Component` per entity | Adds runtime-stats and state fields and a slot in the setup/loop lists | `core/component.h` |
| API connection | ~500–1000 B per connected client, independent of entity count | [esphome.io api](https://esphome.io/components/api/) |

Estimate (not measured): as plain entities (not each a `Component`), roughly 30–60 B of RAM each, so 3–6 KB for 90. Measure with `esphome compile` on a fixture with all 6 pairs before deciding.

Other effects:

- On each API connect, ESPHome sends one list message per entity, then each state.
- No flash writes as long as the switches keep `ALWAYS_OFF` and no entity restores.

## Open points for the spec

- Does the panel take non-zero seconds in a switch time?
- A Modus value outside 0–9: publish no state, or log and keep the last one?
- Aktiv writes: how to serialise 5 switches over one mask ID.
- Sub-devices: does the component's YAML take a `device_id` per pair, or leave it to each entity?
