import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]

sec_touch_ns = cg.esphome_ns.namespace("sec_touch")
SECTouchComponent = sec_touch_ns.class_("SECTouchComponent", cg.Component, uart.UARTDevice)

CONF_SEC_TOUCH_ID = "sec_touch_id"


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SECTouchComponent),
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