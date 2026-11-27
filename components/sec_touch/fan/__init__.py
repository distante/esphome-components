from esphome.components import fan
import esphome.config_validation as cv
import esphome.codegen as cg
import esphome.final_validate as fv
from esphome.const import CONF_OUTPUT_ID
from .. import (
    CONF_FAN_NUMBER,
    CONF_SEC_TOUCH_ID,
    FAN_PAIR_SCHEMA,
    sec_touch_ns,
    SECTouchComponent,
    FAN_LEVEL_IDS,
    FAN_LABEL_IDS,
    sec_touch_fans,
)

DEPENDENCIES = ["sec_touch"]

SecTouchFan = sec_touch_ns.class_("SecTouchFan", cg.Component, fan.Fan)

CONF_SPLIT_SPECIAL_MODES = "split_special_modes"

CONFIG_SCHEMA = cv.All(
    fan.fan_schema(SecTouchFan)
    .extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(SecTouchFan),
            cv.GenerateID(CONF_SEC_TOUCH_ID): cv.use_id(SECTouchComponent),
            cv.Optional(CONF_SPLIT_SPECIAL_MODES, default=False): cv.boolean,
        }
    )
    .extend(FAN_PAIR_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
)


def _final_validate(config):
    fan_number = config[CONF_FAN_NUMBER]
    fans = sec_touch_fans(fv.full_config.get().get("fan"))
    if [fan[CONF_FAN_NUMBER] for fan in fans].count(fan_number) > 1:
        raise cv.Invalid(
            f"{CONF_FAN_NUMBER} {fan_number} is listed twice in fan",
            path=[CONF_FAN_NUMBER],
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    parent = await cg.get_variable(config[CONF_SEC_TOUCH_ID])
    fan_number = config[CONF_FAN_NUMBER]
    level_id = FAN_LEVEL_IDS[fan_number - 1]
    label_id = FAN_LABEL_IDS[fan_number - 1]

    var = cg.new_Pvariable(config[CONF_OUTPUT_ID], parent, level_id, label_id)
    cg.add(var.set_split_special_modes(config[CONF_SPLIT_SPECIAL_MODES]))
    cg.add(parent.register_fan(level_id, var))
    await cg.register_component(var, config)
    await fan.register_fan(var, config)
