from esphome.components import text_sensor
import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import ICON_FAN
from . import (
    CONF_SEC_TOUCH_ID,
    sec_touch_ns,
    SECTouchComponent,
    FAN_LEVEL_IDS,
    FAN_LABEL_IDS,
)


SecTouchFanLabelTextSensor = sec_touch_ns.class_(
    "SecTouchFanLabelTextSensor", cg.Component, text_sensor.TextSensor
)

DEPENDENCIES = ["sec_touch"]

FAN_NUMBER = "fan_number"
CONF_MODE_TEXT = "mode_text"
CONF_LABEL_TEXT = "label_text"

CONFIG_SCHEMA = cv.All(
    cv.COMPONENT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_SEC_TOUCH_ID): cv.use_id(SECTouchComponent),
            cv.Required(FAN_NUMBER): cv.one_of(1, 2, 3, 4, 5, 6),
            cv.Optional(CONF_LABEL_TEXT): text_sensor.text_sensor_schema(
                icon="mdi:tag"
            ),
            cv.Required(CONF_MODE_TEXT): text_sensor.text_sensor_schema(
                icon="mdi:information-variant"
            ),
        }
    ),
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_SEC_TOUCH_ID])
    fan_number = config[FAN_NUMBER]
    level_id = FAN_LEVEL_IDS[fan_number - 1]
    label_id = FAN_LABEL_IDS[fan_number - 1]

    if mode_text_config := config.get(CONF_MODE_TEXT):
        sens = await text_sensor.new_text_sensor(mode_text_config)
        cg.add(parent.register_text_sensor(level_id, sens))

    if label_text_config := config.get(CONF_LABEL_TEXT):
        sens = await text_sensor.new_text_sensor(label_text_config)
        cg.add(parent.register_text_sensor(label_id, sens))
