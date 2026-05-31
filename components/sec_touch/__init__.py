import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]

sec_touch_ns = cg.esphome_ns.namespace("sec_touch")
SECTouchComponent = sec_touch_ns.class_(
    "SECTouchComponent", cg.PollingComponent, uart.UARTDevice
)

FAN_LEVEL_IDS = (173, 174, 175, 176, 177, 178)
FAN_LABEL_IDS = (78, 79, 80, 81, 82, 83)


CONF_SEC_TOUCH_ID = "sec_touch_id"
CONF_INTER_TASK_DELAY = "inter_task_delay_ms"
CONF_GLOBAL_SETTINGS_UPDATE_INTERVAL = "global_settings_update_interval"
CONF_OPTIONAL_CRC = "optional_crc"
# The total of fan pairs that the SEC-Touch shows in the screen

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SECTouchComponent),
        cv.Optional(CONF_INTER_TASK_DELAY, default=30): cv.positive_int,
        cv.Optional(
            CONF_GLOBAL_SETTINGS_UPDATE_INTERVAL, default="30s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_OPTIONAL_CRC, default=False): cv.boolean,
    }
)


CONFIG_SCHEMA = cv.All(
    CONFIG_SCHEMA.extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
    .extend(cv.polling_component_schema("5s")),
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
    cg.add(var.set_inter_task_delay(config[CONF_INTER_TASK_DELAY]))
    cg.add(
        var.set_global_settings_update_interval_ms(
            config[CONF_GLOBAL_SETTINGS_UPDATE_INTERVAL]
        )
    )
    cg.add(var.set_optional_crc(config[CONF_OPTIONAL_CRC]))
