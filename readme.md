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

## TDD With teeth.

We Follow TDD With teeth.
1. We define the test, (if a new API is needed, we define it with a skeleton)
2. We see the test go red due to a real assertion (not build problem)
3. We implement the fix and see the test go green
4. We remove the implementation and the test should go red again with the correct assertion error.
5. We restore the implementation.

## Pull Requests Rules
- We should explain with a TL/DR what was done.
- We include a technical overview of the changes.
- We try to use visual explanations when possible (Mermaid)
