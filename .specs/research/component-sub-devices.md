# Research: can a component give its entities a sub-device by itself?

Answers [#125](https://github.com/distante/esphome-components/issues/125) for the map [#111](https://github.com/distante/esphome-components/issues/111). Follows up on the sub-device row in [time-program-entities.md](https://github.com/distante/esphome-components/blob/research/time-program-entities/.specs/research/time-program-entities.md#grouping-per-fan-pair).

Sources: the ESPHome source vendored at `esphome/` (2026.11.0, commit `1feda1b`; paths below are relative to `esphome/esphome/`), esphome.io, the ESPHome and Home Assistant GitHub repos, and the HA developer docs. "Estimate" marks each claim not read from a source.

## Summary

| Question | Answer |
|---|---|
| Does a component add devices to the build by itself? | No official component does. Two external ones do, through hacks that ESPHome `dev` broke on 2026-11-25. Injecting into the config in `FINAL_VALIDATE` still works, but nothing supports it. |
| Supported way | The user declares each device in `esphome: devices:`, directly or through a shipped package. The component takes one `device_id` per pair and copies it into each entity it builds. |
| HA without sub-devices | Every entity sits on the one node device. `entity_category: config` moves them to their own "Configuration" section and off auto dashboards. A pair prefix in each name keeps them apart (estimate: readable, untested). |

**Recommendation:** one `device_id:` per pair in the sec_touch schema, optional. With it, entities go to the user's sub-device. Without it, they stay on the node with a pair prefix and `entity_category: config`. Ship an example package that declares the devices.

## How core builds devices

| Step | Finding | Source |
|---|---|---|
| Schema | `esphome: devices:` is a list of `DEVICE_SCHEMA`: `id` (`declare_id(Device)`), `name`, optional `area_id`, required `relation` | `core/config.py` `DEVICE_SCHEMA`, `CONFIG_SCHEMA` |
| Codegen | `to_code` reads only `config[CONF_DEVICES]`: defines `USE_DEVICES`, `ESPHOME_DEVICE_COUNT = len(devices)` and the relation defines, then `new_Pvariable` + `App.register_device` per device | `core/config.py` lines 1015–1056 |
| Storage | `App.devices_` is a `StaticVector<Device *, ESPHOME_DEVICE_COUNT>`; `push_back` past the count drops the device silently | `core/application.h`, `core/helpers.h` `StaticVector::push_back` |
| Entity link | `device_id` is in `ENTITY_BASE_SCHEMA` as `sub_device_id` → `use_id(Device)` | `config_validation.py` |
| Entity codegen | `setup_entity` reads `config[CONF_DEVICE_ID]` and emits `var.set_device_(device)` | `core/entity_helpers.py` |
| Name check | The duplicate-name check runs at schema time and keys on `config[CONF_DEVICE_ID]` | `core/entity_helpers.py` `entity_duplicate_validator` |
| Other readers | MQTT discovery and the API wizard read `device_id` from the config, not from C++ | `components/mqtt/__init__.py`, `components/api/wizard.py` |
| Docs | Devices come from the `devices:` list; entities link with `device_id`. Nothing says a component may create one. | [esphome.io core config](https://esphome.io/components/esphome/) |

## Q1: components that add devices themselves

### Search results

| Where | Result | Source |
|---|---|---|
| Vendored `components/` (2026.11.0) | None. No component calls `App.register_device`, declares `Device` or writes `device_id`. `api/wizard.py` and `mqtt` only read it. | grep for `register_device(`, `declare_id(Device)`, `CONF_DEVICES`, `CONF_DEVICE_ID` |
| `ds100_meter` (external) | `to_code` declares its own `Device`, emits `set_device_id`, `set_name`, `App.register_device`, then `entity.set_device(device)`. Sets no count define. | [ds100_meter/\_\_init\_\_.py](https://github.com/maringeph/esphome-custom-components/blob/main/components/ds100_meter/__init__.py) |
| `steinel_mesh` (external) | Defines `USE_DEVICES` and `ESPHOME_DEVICE_COUNT 16` itself and raises `CORE.platform_counts`, to register devices and entities at run time | [steinel_mesh/\_\_init\_\_.py](https://github.com/supczinskib/steinel-mesh-gateway/blob/main/esphome/components/steinel_mesh/__init__.py) |
| ESPHome PRs and feature requests | No plan for component-made devices. A run-time entity experiment was closed. | [esphome#23242](https://github.com/esphome/esphome/pull/23242) (closed), `gh search` on esphome/esphome and esphome/feature-requests |

### Stability of each route

| Route | 2026.11.0 | ESPHome `dev` after [#23234](https://github.com/esphome/esphome/pull/23234) (merged 2026-11-25) | Source |
|---|---|---|---|
| Codegen `App.register_device` (ds100) | Works only when the user also declares devices (the count define comes from core). Past that count, devices drop silently. `set_device` without `_` no longer exists. | Build error: `register_device()` is removed; "devices and areas come from the YAML `devices:` and `areas:` lists" | `core/application.h`, `core/entity_base.h`, #23234 body |
| Own `ESPHOME_DEVICE_COUNT` (steinel) | Two `#define`s with different values when the user declares devices too | Codegen error: "a conflicting count define is a codegen error" | `core/__init__.py` `Define`, `writer.py`, #23234 `app_lists.py` |
| Append to `full_config["esphome"]["devices"]` in `FINAL_VALIDATE` | Works (estimate: not built). Core `to_code` emits it with the right count and relation defines. | Same: core still reads `config[CONF_DEVICES]` and adds each to the list | `core/config.py`, #23234 diff of `core/config.py` |

The `FINAL_VALIDATE` route, in detail:

| Fact | Source |
|---|---|
| Final validation may rewrite the full config: `esphome` OTA and `sntp` replace their own domain in it | `components/esphome/ota/__init__.py`, `components/sntp/time.py` |
| Final validation runs after the ID pass (priority −20 vs −10) | `config.py` `IDPassValidationStep`, `FinalValidateValidationStep` |
| Core's device hash-collision check (`CoreFinalValidateStep`) has the same priority but is queued first, so it never sees an injected device | `config.py` `validate_config`, `_ValidationStepTask` order |
| Entities must get `device_id` in the same hook, after the duplicate-name check, so names must be unique across pairs anyway | `core/entity_helpers.py` |
| The injected dict must match `DEVICE_SCHEMA` output, which changed in 2026.11.0 (`relation` became required) | [esphome#21232](https://github.com/esphome/esphome/pull/21232) |
| No official component writes another domain's config | grep of `components/` for writes to `full_conf[...]` |

Verdict: it works today but relies on undocumented internals that just changed twice. Not recommended.

## Q2: the supported way

| Piece | How | Source |
|---|---|---|
| Device | User adds `esphome: devices: - {id: pair_1_tp, name: "Pair 1 time program", relation: child}` | [esphome.io core config](https://esphome.io/components/esphome/) |
| Less typing | Ship a package per pair with `vars`; packages concatenate lists, so `esphome: devices:` entries merge with the user's own | [esphome.io packages](https://esphome.io/components/packages/), `config_helpers.py` `merge_config` (lists: `old + new`) |
| One `device_id` for many entities | Component schema takes `cv.Optional(CONF_DEVICE_ID): cv.sub_device_id`. A validator placed before the entity sub-schemas copies the value into each entity's raw config (unless set). Each entity then validates it as its own `device_id`. | `config_validation.py` `sub_device_id`; plain voluptuous `cv.All` (estimate: no vendored precedent) |
| Why copy into the config | The per-device name check, MQTT and the API wizard read `config[CONF_DEVICE_ID]`. A bare `set_device_` call in `to_code` would skip them. | `core/entity_helpers.py`, `components/mqtt/__init__.py`, `components/api/wizard.py` |
| Same entity names per pair | Allowed once each pair has its own device: the name check is per device | `core/entity_helpers.py` `entity_duplicate_validator`; [esphome#9355](https://github.com/esphome/esphome/pull/9355) |

Sketch (estimate, not built):

```yaml
esphome:
  devices:
    - id: pair_1_tp
      name: "Pair 1 time program"
      relation: child

sec_touch:
  time_programs:
    - pair: 1
      device_id: pair_1_tp
```

## Q3: Home Assistant without sub-devices

| Topic | Finding | Source |
|---|---|---|
| Device | All entities sit on the node's device | HA `components/esphome/entity.py` (`ChildDeviceInfo` only for a `device_id`) |
| Name | ESPHome entities set `has_entity_name = True`, so HA shows the device name plus the entity name, such as `Ventilation Pair 1 Schaltzeit 1 Zeit` | [HA esphome entity.py](https://github.com/home-assistant/core/blob/dev/homeassistant/components/esphome/entity.py), [HA entity_registry.py](https://github.com/home-assistant/core/blob/dev/homeassistant/helpers/entity_registry.py) |
| `entity_category: config` | Own section on the device page; not on auto-generated dashboards; not exposed to voice assistants; actions that target the whole device or area skip them | [HA entity registry docs](https://contributors.home-assistant.io/core/entities/registry/) |
| Allowed on | `select`, `switch`, `datetime`: yes. Sensors and binary sensors cannot be `config`. | same |
| Name length | Entity names fit ESPHome's 120-byte limit easily | `core/config.py` `FRIENDLY_NAME_MAX_LEN` |

Readability (estimate, not checked in HA): 90 entities in one "Configuration" section is a long list, but a fixed prefix `Pair N Schaltzeit M` keeps each pair's 15 entities together. With sub-devices, each pair's device page holds only its 15.

## Open points for the spec

- Make `device_id` per pair optional, with the prefix fallback?
- Ship an example package that declares the six pair devices?
- Entity names: with a device, `Schaltzeit 1 Zeit`; without, `Pair 1 Schaltzeit 1 Zeit`. Does the component pick by the presence of `device_id`?
