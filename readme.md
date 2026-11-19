# Components
- [SEC-Touch Ventilation System](/components/sec_touch)

# Tests

All five layers run in CI ([.github/workflows/tests.yml](.github/workflows/tests.yml)) on every PR and push to `main`.

One-time setup, from the repo root:

```sh
git submodule update --init      # the vendored ESPHome
python3.12 -m venv .venv && . .venv/bin/activate
pip install -r requirements_test.txt
```

| Layer | Command | Tests live in | Use it for |
| --- | --- | --- | --- |
| C++ unit tests (PlatformIO `native` + GoogleTest) | `pio test -e native` | `tests/cpp/test_<name>/` | Logic in a pure header (a seam), no ESPHome includes. The default. |
| Host component (real ESPHome core on the `host` platform + GoogleTest) | `python tests/host/run.py [component]` | `tests/host/<component>/` | Whole-component behaviour no seam can hold: bytes in through a fake UART, `loop()`, bytes and callbacks out. |
| Config validation (`esphome config` + pytest) | `pytest tests/config` | `tests/config/{valid,invalid}/*.yaml` | YAML schema and validation errors. |
| Config compile (`esphome compile` + pytest, opt-in) | `pytest tests/config --compile -m compile` | `tests/config/valid/*.yaml` | Include and `AUTO_LOAD` bugs: a header that needs a component the config left out. |
| Page JS (`node --test`, Node 22, no dependencies) | `npm test` | `components/<component>/web/*.test.mjs` | Page logic in DOM-free ES modules. |

The host layer reuses ESPHome's own C++ unit test runner (`esphome/script/cpp_unit_test.py`, an internal API that may change on an ESPHome bump), so it needs the editable ESPHome install from the setup above and a C++ compiler with AddressSanitizer (clang on macOS, gcc on Linux). The first run downloads GoogleTest. When a test needs a platform, a `<domain>/` subfolder such as `tests/host/sec_touch/fan/` pulls it into the build.

Invalid config fixtures start with `# expect-error: <text>`; the test checks that `esphome config` fails and prints that text.

The compile layer builds every valid fixture, because `esphome config` never compiles and so can't catch a header that includes a component missing from the build (a missing `AUTO_LOAD`). Without `--compile`, `pytest tests/config` skips it. The first run downloads ESP-IDF and takes several minutes. When a component's C++ uses another ESPHome component, add a valid fixture that leaves that component out of the config.

## TDD With teeth.

We Follow TDD With teeth.
1. We define the test, (if a new API is needed, we define it with a skeleton)
2. We see the test go red due to a real assertion (not build problem)
3. We implement the fix and see the test go green
4. We remove the implementation and the test should go red again with the correct assertion error.
5. We restore the implementation.

## Pull Requests Rules

All pull request rules (title, body template, visuals, evidence) live in [.claude/skills/pr-body/SKILL.md](.claude/skills/pr-body/SKILL.md).
