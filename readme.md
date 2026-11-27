# Components
- [SEC-Touch Ventilation System](/components/sec_touch)

# Linting

[pre-commit](https://pre-commit.com) runs every linter and formatter: clang-format (`.clang-format`), yamllint (`.yamllint`), flake8 (`.flake8`), and checks for trailing whitespace, final newlines and LF line endings. CI runs the same hooks ([.github/workflows/lint.yml](.github/workflows/lint.yml)) on every PR and push to `main`.

One-time setup, inside the venv from [Tests](#tests):

```sh
pip install -r requirements_dev.txt
pre-commit install               # runs the hooks on every git commit
git config blame.ignoreRevsFile .git-blame-ignore-revs   # git blame skips formatting-only commits
```

`pre-commit run --all-files` checks the whole repo by hand. A hook that fixes files (such as clang-format) fails the commit; stage its changes and commit again.

Every tool is pinned. `.pre-commit-config.yaml` pins each hook, and pre-commit installs each one into its own cached environment (`~/.cache/pre-commit`), so the versions here never touch tools another project installs. clang-format and yamllint use the same versions as the vendored ESPHome. `requirements_dev.txt` also puts that clang-format into the venv, for the editor's format-on-save; keep its pin in step with the hook's `rev`.

`.vscode/settings.json` points at `.venv/bin/clang-format`. On Windows, set `C_Cpp.clang_format_path` to `${workspaceFolder}/.venv/Scripts/clang-format.exe` in your user settings.

# Tests

Every row of the table below runs in CI ([.github/workflows/tests.yml](.github/workflows/tests.yml)) on every PR and push to `main`.

One-time setup, from the repo root:

```sh
git submodule update --init      # the vendored ESPHome
python3.12 -m venv .venv && . .venv/bin/activate
pip install -r requirements_test.txt
```

| Layer | Command | Tests live in |
| --- | --- | --- |
| C++ unit tests (PlatformIO `native` + GoogleTest) | `pio test -e native` | `tests/cpp/test_<name>/` |
| Host component (real ESPHome core on the `host` platform + GoogleTest) | `python tests/host/run.py [component]` | `tests/host/<component>/` |
| Config validation (`esphome config` + pytest) | `pytest tests/config` | `tests/config/{valid,invalid}/*.yaml` |
| Config compile (`esphome compile` + pytest, opt-in) | `pytest tests/config --compile -m compile` | `tests/config/valid/*.yaml` |
| Page JS (`node --test`, Node 22, no dependencies) | `npm test` | `components/<component>/web/*.test.mjs` |
| Build scripts (pytest; the sniffer page bundle and its catalogue) | `pytest tests/python` | `tests/python/test_*.py` |

Which layer a change needs, and the TDD With teeth steps: [.claude/skills/coding-standards/SKILL.md](.claude/skills/coding-standards/SKILL.md#tests).

The host layer reuses ESPHome's own C++ unit test runner (`esphome/script/cpp_unit_test.py`, an internal API that may change on an ESPHome bump), so it needs the editable ESPHome install from the setup above and a C++ compiler with AddressSanitizer (clang on macOS, gcc on Linux). The first run downloads GoogleTest. When a test needs a platform, a `<domain>/` subfolder such as `tests/host/sec_touch/fan/` pulls it into the build. A domain the component has no platform for (`datetime`, `event`) goes into the suite's `benchmark.yaml` instead; there the `time:` key defines `USE_DATETIME_TIME`. The host build uses `-Werror=format`: on a 64-bit host `size_t` is `unsigned long`, so a `%d` for a `.size()` fails here even though ESP32 builds stay quiet. A suite can include another suite's helpers (the sniffer uses `sec_touch/common.h`); list that in `TEST_HELPERS_FROM` in `tests/host/run.py` so the folder reaches the build.

Invalid config fixtures start with one or more `# expect-error: <text>` lines; the test checks that `esphome config` fails and prints each text.

The compile layer builds every valid fixture, because `esphome config` never compiles and so can't catch a header that includes a component missing from the build (a missing `AUTO_LOAD`). Without `--compile`, `pytest tests/config` skips it. The first run downloads ESP-IDF and takes several minutes. CI builds each valid fixture in its own parallel job, one per test pytest collects. CI turns a fixture's name (`sec_touch_sniffer_with_time.yaml`) into the job name (`sec_sniffer: with time`).

The compile layer also fails on any compiler warning from `components/`. Each run starts with `esphome clean`, because an incremental build prints no warnings for files it skips. Every valid fixture sets `logger: level: VERY_VERBOSE`; without it, ESPHome compiles out every `ESP_LOG*` call and `-Wformat` never fires. The test checks that each fixture keeps it.

The sniffer page (`components/sec_touch_sniffer/web/`) can be previewed without flashing. Bundle it into one file the way the build does, serve it locally, and point it at a device with `?device=`:

```sh
python -c "import sys; sys.path.insert(0, 'components/sec_touch_sniffer'); from page_bundle import bundle_page; open('sniffer.html', 'w').write(bundle_page())"
python -m http.server 8000   # then open http://localhost:8000/sniffer.html?device=http://<device-ip>
```

The device's `/sniffer/data` sends `Access-Control-Allow-Origin: *`, so a desktop browser works once it grants local-network access. Headless Chrome stays offline against a LAN device, so screenshots need a small local proxy that serves the page and forwards `/sniffer/*` to the device on the same origin.

## Coding standards

All coding standards (C++ style, memory, tests, TDD With teeth, checklists) live in [.claude/skills/coding-standards/SKILL.md](.claude/skills/coding-standards/SKILL.md).

## Pull Requests Rules

All pull request rules (title, body template, visuals, evidence) live in [.claude/skills/pr-body/SKILL.md](.claude/skills/pr-body/SKILL.md).
