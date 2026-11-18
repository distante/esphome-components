"""`--compile` opts into the slow `esphome compile` tests (minutes per fixture)."""

import pytest


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption(
        "--compile",
        action="store_true",
        default=False,
        help="also compile every valid fixture with `esphome compile`",
    )


def pytest_configure(config: pytest.Config) -> None:
    config.addinivalue_line(
        "markers", "compile: runs `esphome compile`; needs --compile"
    )


def pytest_collection_modifyitems(
    config: pytest.Config, items: list[pytest.Item]
) -> None:
    if config.getoption("--compile"):
        return
    skip = pytest.mark.skip(reason="needs --compile")
    for item in items:
        if "compile" in item.keywords:
            item.add_marker(skip)
