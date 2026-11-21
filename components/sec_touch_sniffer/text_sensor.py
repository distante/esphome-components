from pathlib import Path
import re

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import text_sensor, switch, web_server_base
from esphome.const import CONF_ID, CONF_TIME_ID
from esphome.core import CORE

from esphome.components.sec_touch import CONF_SEC_TOUCH_ID, sec_touch_ns, SECTouchComponent

from .page_bundle import gzipped_page

DEPENDENCIES = ["sec_touch"]
AUTO_LOAD = ["switch"]

CONF_SCAN_START = "scan_start"
CONF_SCAN_END = "scan_end"
CONF_SCAN_SWITCH = "scan_switch"
CONF_WEB_PAGE = "web_page"


# The component that serves pages.
WEB_SERVER_BASE = web_server_base.DOMAIN

# HTTP clients the page keeps open: one for the polling tab, one spare for a reload or keep-alive.
WEB_PAGE_CLIENTS = 2

# Compiles the /sniffer handler; page-off builds carry no server code.
WEB_PAGE_DEFINE = "USE_SEC_TOUCH_SNIFFER_WEB_PAGE"

# The gzipped page in flash, and `<PAGE_ARRAY>_SIZE`; sec_touch_sniffer.h declares both extern.
PAGE_ARRAY = "SEC_TOUCH_SNIFFER_PAGE"


def _read_max_property_id():
    """MAX_PROPERTY_ID from property_slots.h, the single definition the C++ store uses."""
    header = (Path(__file__).parent / "property_slots.h").read_text()
    match = re.search(r"constexpr int MAX_PROPERTY_ID = (\d+);", header)
    if match is None:
        raise RuntimeError("MAX_PROPERTY_ID not found in property_slots.h")
    return int(match.group(1))


MAX_PROPERTY_ID = _read_max_property_id()

SecTouchSniffer = sec_touch_ns.class_(
    "SecTouchSniffer", cg.Component, text_sensor.TextSensor
)
SecTouchSnifferScanSwitch = sec_touch_ns.class_(
    "SecTouchSnifferScanSwitch", switch.Switch
)


def _validate_esp32(config):
    if not CORE.is_esp32:
        raise cv.Invalid("sec_touch_sniffer is only available on ESP32")
    return config


def _validate_scan_range(config):
    if config.get(CONF_SCAN_END, 0) > 0:
        if config[CONF_SCAN_START] > config[CONF_SCAN_END]:
            raise cv.Invalid(
                f"scan_start ({config[CONF_SCAN_START]}) must be <= scan_end ({config[CONF_SCAN_END]})"
            )
    return config


def _add_web_page_clients(config):
    # Counts the sockets (socket.consume_sockets) and raises the httpd client limit to match, as web_server does.
    if config[CONF_WEB_PAGE]:
        web_server_base.add_httpd_clients(WEB_PAGE_CLIENTS, "sec_touch_sniffer")
    return config


CONFIG_SCHEMA = cv.All(
    text_sensor.text_sensor_schema(SecTouchSniffer)
    .extend(
        {
            cv.GenerateID(): cv.declare_id(SecTouchSniffer),
            cv.Required(CONF_SEC_TOUCH_ID): cv.use_id(SECTouchComponent),
            cv.Optional(CONF_TIME_ID): cv.invalid(
                "time_id was removed: sniffer timestamps are device uptime, and the sniffer page shows wall time"
            ),
            cv.Optional(CONF_SCAN_START, default=1): cv.int_range(min=0, max=MAX_PROPERTY_ID),
            cv.Optional(CONF_SCAN_END, default=0): cv.int_range(min=0, max=MAX_PROPERTY_ID),
            cv.Optional(CONF_SCAN_SWITCH): switch.switch_schema(SecTouchSnifferScanSwitch),
            cv.Optional(CONF_WEB_PAGE, default=False): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    _validate_esp32,
    _validate_scan_range,
    _add_web_page_clients,
)


def _final_validate(config):
    # web_server_base is not auto-loaded, so page-off builds carry no server code. web_server and the other
    # components that serve pages load it themselves.
    if config[CONF_WEB_PAGE] and WEB_SERVER_BASE not in fv.full_config.get():
        raise cv.Invalid(
            f"web_page: true needs an HTTP server: add `{WEB_SERVER_BASE}:` (or `web_server:`)",
            path=[CONF_WEB_PAGE],
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    parent = await cg.get_variable(config[CONF_SEC_TOUCH_ID])
    var = cg.new_Pvariable(config[CONF_ID], parent)
    await cg.register_component(var, config)
    await text_sensor.register_text_sensor(var, config)

    cg.add(var.set_web_page(config[CONF_WEB_PAGE]))
    if config[CONF_WEB_PAGE]:
        cg.add_define(WEB_PAGE_DEFINE)
        # Bundled and gzipped at every build, as web_server does with js_include, so flash always holds web/.
        page = gzipped_page()
        cg.extern_progmem_array(PAGE_ARRAY, cg.uint8, list(page))
        cg.add_global(cg.RawExpression(f"constexpr size_t {PAGE_ARRAY}_SIZE = {len(page)}"))

    scan_end = config[CONF_SCAN_END]
    if scan_end > 0:
        cg.add(var.set_scan_range(config[CONF_SCAN_START], scan_end))

    if CONF_SCAN_SWITCH in config:
        sw = await switch.new_switch(config[CONF_SCAN_SWITCH])
        await cg.register_parented(sw, var)
        cg.add(var.set_scan_switch(sw))
