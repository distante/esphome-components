# Components
- [SEC-Touch Ventilation System](/components/sec_touch)
# Tests

All three layers run in CI ([.github/workflows/tests.yml](.github/workflows/tests.yml)) on every PR and push to `main`.

One-time setup, from the repo root:

```sh
git submodule update --init      # the vendored ESPHome
python3.12 -m venv .venv && . .venv/bin/activate
pip install -r requirements_test.txt
```

| Layer | Command | Tests live in |
| --- | --- | --- |
| C++ unit tests (PlatformIO `native` + GoogleTest) | `pio test -e native` | `tests/cpp/test_<name>/` |
| Config validation (`esphome config` + pytest) | `pytest tests/config` | `tests/config/{valid,invalid}/*.yaml` |
| Page JS (`node --test`, Node 22, no dependencies) | `npm test` | `components/<component>/web/*.test.mjs` |

Invalid config fixtures start with `# expect-error: <text>`; the test checks that `esphome config` fails and prints that text.
