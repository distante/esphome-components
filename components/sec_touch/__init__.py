import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]

sec_touch_ns = cg.esphome_ns.namespace("sec_touch")
SECTouchComponent = sec_touch_ns.class_(
    "SECTouchComponent", cg.Component, uart.UARTDevice
)

CONF_SEC_TOUCH_ID = "sec_touch_id"
# The total of fan pairs that the SEC-Touch shows in the screen
CONF_TOTAL_FAN_PAIRS = "total_fan_pairs"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SECTouchComponent),
        cv.Required(CONF_TOTAL_FAN_PAIRS): cv.one_of(1, 2, 3, 4, 5, 6),
    }
)


CONFIG_SCHEMA = cv.All(
    CONFIG_SCHEMA.extend(uart.UART_DEVICE_SCHEMA).extend(cv.COMPONENT_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "sec_touch",
    require_tx=True,
    require_rx=True,
    # parity="NONE",
    # stop_bits=1,
    baud_rate=28800,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    total_fan_pairs = config[CONF_TOTAL_FAN_PAIRS]
    cg.add(var.set_total_fan_pairs(total_fan_pairs))
