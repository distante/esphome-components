# How ESPHome deprecates or moves a platform or config key

Research for [#71](https://github.com/distante/esphome-components/issues/71), feeding [#72](https://github.com/distante/esphome-components/issues/72) (how the sniffer retires its text sensor). Facts only; no decision.

## Version this repo builds against

- `esphome` is a git submodule pinned at `1feda1bcaa861a5f2e933f2c65d599e44f4b132b`, which is identical to tag [`2026.11.0`](https://github.com/esphome/esphome/tree/2026.11.0) (`gh api repos/esphome/esphome/compare/2026.11.0...1feda1b` → `identical`; `esphome/const.py` has `__version__ = "2026.11.0"`).
- `requirements_test.txt` installs it with `-e ./esphome`. `requirements_dev.txt` holds only lint tools.

All source links below are to tag `2026.11.0`.

## Official policy (developers.esphome.io)

From [developers.esphome.io/contributing/code](https://developers.esphome.io/contributing/code/), sections "What Constitutes a Python Breaking Change?" and "Python Deprecation Process":

- Python breaking changes include "Removing a documented configuration key", "Renaming a documented configuration key" and "Removing a platform or component".
- Process: add a deprecation warning **during validation**, keep the old config working "for 6 months when possible", document the migration in the warning and the PR description, update esphome.io examples.
- Put a `Remove before YYYY.MM.0` comment at the deprecation site, with the date 6 months after the merge.
- Warnings say what is deprecated, what replaces it, the removal version and a migration link. "Provide automatic migration when possible."
- After the period, the old key becomes a `cv.invalid(...)` error naming the replacement.
- A "clean break" is allowed: skip the warning and document the breaking change with migration examples in the PR description.

This policy binds core; an external component like ours is free to choose, but it is the convention users know.

## Helpers ESPHome core offers (2026.11.0)

All in [`esphome/config_validation.py`](https://github.com/esphome/esphome/blob/2026.11.0/esphome/config_validation.py):

| Helper | What it does |
|---|---|
| `cv.invalid(message)` | Validator that always raises `Invalid(message)`. Used as a whole `CONFIG_SCHEMA` (hard-removes a platform/component) or as the value of `cv.Optional(key)` (hard-removes a key). |
| `cv.rename_key(old, new, *, removed_in=None, component=None)` | Moves `old` to `new` inside a dict, errors if both given (via `has_at_most_one_key`). With `removed_in`, logs `"[component] 'old' is deprecated, use 'new'. Will be removed in X"`. Key renames only, within one schema. |
| `cv.has_at_most_one_key`, `has_exactly_one_key`, `has_at_least_one_key`, `has_none_or_all_keys` | Key-combination checks; useful when old and new spellings may both appear. |
| `cv.Any` | Plain alias of `vol.Any` (accept either schema). |
| `LEGACY_CONFIG_MIGRATE` (module attribute) | Loader hook ([`loader.py` `legacy_config_migrate`](https://github.com/esphome/esphome/blob/2026.11.0/esphome/loader.py), called in [`config.py`](https://github.com/esphome/esphome/blob/2026.11.0/esphome/config.py)) letting a **platform component** rewrite its own raw top-level config into `platform:` entries. Only `image` uses it. Not applicable to moving a platform entry out of `text_sensor:`. |

Not present: there is **no `cv.removed`**, no `deprecated=` argument on `cv.Optional`/`cv.Required`, and no generic "platform moved" helper. `DEPRECATED_COMPONENT = """..."""` strings exist in several display components' `__init__.py`, but nothing in core's config loader reads them at 2026.11.0; the actual warning is a hand-written `LOGGER.warning`.

Everything else is hand-rolled: a validator in `cv.All(...)` that calls `_LOGGER.warning(...)` and returns (possibly rewritten) config, or a warning in `to_code`.

## What core components did

### Hard error with migration text (`cv.invalid` as the whole `CONFIG_SCHEMA`)

The old platform file is kept only to print where to go. No warning period.

| Old config | Message points to | Version | Source |
|---|---|---|---|
| `sensor: - platform: dallas` and top-level `dallas:` | `dallas_temp` platform / `one_wire:` | 2024.6.0 ([#6860](https://github.com/esphome/esphome/pull/6860), `breaking-change`) | [dallas/sensor.py](https://github.com/esphome/esphome/blob/2026.11.0/esphome/components/dallas/sensor.py), [dallas/\_\_init\_\_.py](https://github.com/esphome/esphome/blob/2026.11.0/esphome/components/dallas/__init__.py) |
| `sensor:`/`binary_sensor: - platform: udp` | `packet_transport` platforms | 2025.5.0 ([#8187](https://github.com/esphome/esphome/pull/8187), `breaking-change`) | [udp/sensor.py](https://github.com/esphome/esphome/blob/2026.11.0/esphome/components/udp/sensor.py) |
| `custom` platforms / `custom_component:` | external components (doc link) | 2025.2.0 ([#8119](https://github.com/esphome/esphome/pull/8119), "As documented", removal announced in docs beforehand) | [custom_component/\_\_init\_\_.py](https://github.com/esphome/esphome/blob/2026.11.0/esphome/components/custom_component/__init__.py) |
| `climate: - platform: midea_ac` | `midea` | 2021.9 ([#2188](https://github.com/esphome/esphome/pull/2188)) | [midea_ac/climate.py](https://github.com/esphome/esphome/blob/2026.11.0/esphome/components/midea_ac/climate.py) |
| `sensor: - platform: xiaomi_miscale2` | `xiaomi_miscale` | 2021.10 ([#2266](https://github.com/esphome/esphome/pull/2266)) | [xiaomi_miscale2/sensor.py](https://github.com/esphome/esphome/blob/2026.11.0/esphome/components/xiaomi_miscale2/sensor.py) |
| top-level `xpt2046:` | `touchscreen:` platform | — | [xpt2046/\_\_init\_\_.py](https://github.com/esphome/esphome/blob/2026.11.0/esphome/components/xpt2046/__init__.py) |
| `binary_sensor: - platform: cst816` | touchscreen binary sensor with `use_raw` | — | [cst816/binary_sensor.py](https://github.com/esphome/esphome/blob/2026.11.0/esphome/components/cst816/binary_sensor.py) |
| `display: - platform: inkplate6` | `inkplate` | 2025.9.0 ([#10200](https://github.com/esphome/esphome/pull/10200)) | [inkplate6/display.py](https://github.com/esphome/esphome/blob/2026.11.0/esphome/components/inkplate6/display.py) |

Typical shape:

```python
import esphome.config_validation as cv

CONFIG_SCHEMA = cv.invalid(
    'The "dallas" sensor is now "dallas_temp"\nhttps://esphome.io/components/sensor/dallas_temp'
)
```

Note the dallas folder holds **both** a top-level `__init__.py` and a `sensor.py` platform, each with its own `CONFIG_SCHEMA`. One component directory can serve `dallas:` and `sensor: - platform: dallas` at once; that is also how `sec_touch_sniffer/__init__.py` + `text_sensor.py` could coexist.

### Warn, keep working, remove later

| Change | How | Removal | Source |
|---|---|---|---|
| Top-level `image:`, `animation:`, `online_image:` → platforms of `image:` (the reverse direction of our move) | Old top-level schema still accepted; capture validator in `CONFIG_SCHEMA` records each entry, `FINAL_VALIDATE_SCHEMA` warns **once** and prints a ready-to-paste migrated YAML block (`legacy_platform_migration_warning`). Shims isolated "so they can be deleted in one piece". Landed in 2026.7.0 ([#17416](https://github.com/esphome/esphome/pull/17416), labelled a new feature, not breaking). | 2027.1.0 | [image/\_\_init\_\_.py](https://github.com/esphome/esphome/blob/2026.11.0/esphome/components/image/__init__.py), [animation/\_\_init\_\_.py](https://github.com/esphome/esphome/blob/2026.11.0/esphome/components/animation/__init__.py) |
| `select: - platform: espectre` configured directly → nested under `traffic_generator_mode:` | Validator first in `cv.All` logs a warning and rewrites the config to the new shape; comment `# Remove before 2027.5.0`. Landed in 2026.11.0 ([#22132](https://github.com/esphome/esphome/pull/22132)). | 2027.5.0 | [espectre/select.py](https://github.com/esphome/esphome/blob/2026.11.0/esphome/components/espectre/select.py) |
| `voc`/`nox` → `voc_index`/`nox_index` in sgp4x, sen5x, sen6x | `cv.rename_key(..., removed_in="2027.2.0", component=...)` | 2027.2.0 | [sgp4x/sensor.py](https://github.com/esphome/esphome/blob/2026.11.0/esphome/components/sgp4x/sensor.py) |
| hm3301 `aqi:` key → standalone `aqi` sensor platform | Old key still generates code; `LOGGER.warning` in `to_code` under `# Remove before 2026.12.0` | 2026.12.0 | [hm3301/sensor.py](https://github.com/esphome/esphome/blob/2026.11.0/esphome/components/hm3301/sensor.py) |
| Whole display drivers (`st7789v`, `ili9xxx`, `waveshare_epaper`, ...) → `mipi_spi` | `LOGGER.warning` in `to_code`: "deprecated and will be removed in ESPHome 2027.5.0, migrate to 'mipi_spi'" | 2027.5.0 | [st7789v/display.py](https://github.com/esphome/esphome/blob/2026.11.0/esphome/components/st7789v/display.py) |
| `cv.only_on_rp2040` (developer API) | Warns once per run via a `CORE.data` flag | 2027.7.0 | [config_validation.py](https://github.com/esphome/esphome/blob/2026.11.0/esphome/config_validation.py) |

Once the window ends, keys become `cv.Optional(KEY): cv.invalid("... use X instead")`: e.g. haier `beeper`, spi `force_sw`, speed fan `speed` ("Configuring individual speeds is deprecated."), ld2450 `throttle`.

## Patterns worth noting

- **Where warnings fire.** Schema validators (`CONFIG_SCHEMA`/`FINAL_VALIDATE_SCHEMA`) warn on `esphome config` as well as `compile`; `to_code` warnings fire only when generating code. Policy asks for validation-time warnings.
- **Warn once.** Validators run per entry and can run more than once; core dedupes with a `CORE.data` flag (`only_on_rp2040`, image's `legacy_warning_shown`).
- **Pasteable migration.** The image move prints the exact replacement YAML via `yaml_util.dump`. That is the most user-friendly precedent for a platform ↔ top-level move.
- **Recent trend (2026):** platform/top-level restructures (image, espectre) went warn-then-remove with ~6 months; older moves of platforms between components (dallas 2024.6, udp 2025.5) were clean breaks with a `cv.invalid` pointer, labelled `breaking-change`.
