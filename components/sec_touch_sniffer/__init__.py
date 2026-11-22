import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import web_server_base
from esphome.const import CONF_ID
from esphome.core import CORE

from esphome.components.sec_touch import CONF_SEC_TOUCH_ID, sec_touch_ns, SECTouchComponent

from .page_bundle import gzipped_page

DEPENDENCIES = ["sec_touch"]

# The component that serves pages.
WEB_SERVER_BASE = web_server_base.DOMAIN

# HTTP clients the page keeps open: one for the polling tab, one spare for a reload or keep-alive.
WEB_PAGE_CLIENTS = 2

# The gzipped page in flash, and `<PAGE_ARRAY>_SIZE`; sec_touch_sniffer.h declares both extern.
PAGE_ARRAY = "SEC_TOUCH_SNIFFER_PAGE"

SecTouchSniffer = sec_touch_ns.class_("SecTouchSniffer", cg.Component)


def _validate_esp32(config):
    if not CORE.is_esp32:
        raise cv.Invalid("sec_touch_sniffer is only available on ESP32")
    return config


def _add_web_page_clients(config):
    # Counts the sockets (socket.consume_sockets) and raises the httpd client limit to match, as web_server does.
    web_server_base.add_httpd_clients(WEB_PAGE_CLIENTS, "sec_touch_sniffer")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SecTouchSniffer),
            cv.GenerateID(CONF_SEC_TOUCH_ID): cv.use_id(SECTouchComponent),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_esp32,
    _add_web_page_clients,
)


def _final_validate(config):
    # web_server_base is not auto-loaded: web_server and the other components that serve pages load it themselves.
    if WEB_SERVER_BASE not in fv.full_config.get():
        raise cv.Invalid(f"sec_touch_sniffer serves its page over HTTP: add `{WEB_SERVER_BASE}:` (or `web_server:`)")
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    parent = await cg.get_variable(config[CONF_SEC_TOUCH_ID])
    var = cg.new_Pvariable(config[CONF_ID], parent)
    await cg.register_component(var, config)

    # Bundled and gzipped at every build, as web_server does with js_include, so flash always holds web/.
    page = gzipped_page()
    cg.extern_progmem_array(PAGE_ARRAY, cg.uint8, list(page))
    cg.add_global(cg.RawExpression(f"constexpr size_t {PAGE_ARRAY}_SIZE = {len(page)}"))
