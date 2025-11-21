from esphome.components import fan
import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import CONF_OUTPUT_ID
from .. import (
    CONF_SEC_TOUCH_ID,
    sec_touch_ns,
    SECTouchComponent,
    FAN_LEVEL_IDS,
    FAN_LABEL_IDS,
)

DEPENDENCIES = ["sec_touch"]

SecTouchFan = sec_touch_ns.class_("SecTouchFan", cg.Component, fan.Fan)

FAN_NUMBER = "fan_number"

CONFIG_SCHEMA = cv.All(
    fan.fan_schema(SecTouchFan)
    .extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(SecTouchFan),
            cv.GenerateID(CONF_SEC_TOUCH_ID): cv.use_id(SECTouchComponent),
            cv.Required(FAN_NUMBER): cv.one_of(1, 2, 3, 4, 5, 6),
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_SEC_TOUCH_ID])
    fan_number = config[FAN_NUMBER]
    level_id = FAN_LEVEL_IDS[fan_number - 1]
    label_id = FAN_LABEL_IDS[fan_number - 1]

    var = cg.new_Pvariable(config[CONF_OUTPUT_ID], parent, level_id, label_id)
    await cg.register_component(var, config)
    await fan.register_fan(var, config)
