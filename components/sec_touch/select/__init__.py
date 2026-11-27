from esphome.components import select
import esphome.config_validation as cv
import esphome.codegen as cg
import esphome.final_validate as fv
from .. import (
    CONF_FAN_NUMBER,
    CONF_SEC_TOUCH_ID,
    FAN_NUMBER_SCHEMA,
    sec_touch_ns,
    SECTouchComponent,
    FAN_LEVEL_IDS,
    sec_touch_fans,
)

DEPENDENCIES = ["sec_touch"]

SecTouchModeSelect = sec_touch_ns.class_(
    "SecTouchModeSelect", cg.Component, select.Select
)

CONF_MODE_SELECT = "mode_select"

# Order mirrors fan/_fan_mode.cpp FanModeList; "Off" is the additional first option.
MODE_OPTIONS = [
    "Off",
    "Normal",
    "Burst",
    "Automatic Humidity",
    "Automatic CO2",
    "Automatic Time",
    "Sleep",
]

CONFIG_SCHEMA = cv.All(
    cv.COMPONENT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_SEC_TOUCH_ID): cv.use_id(SECTouchComponent),
            cv.Required(CONF_FAN_NUMBER): FAN_NUMBER_SCHEMA,
            cv.Required(CONF_MODE_SELECT): select.select_schema(
                SecTouchModeSelect,
                icon="mdi:tune",
            ),
        }
    ),
)


def _final_validate(config):
    sec_touch_id = config[CONF_SEC_TOUCH_ID]
    fan_numbers_with_sec_touch = {
        entry.get(CONF_FAN_NUMBER)
        for entry in sec_touch_fans(fv.full_config.get().get("fan"))
        if entry.get(CONF_SEC_TOUCH_ID) == sec_touch_id
    }
    n = config[CONF_FAN_NUMBER]
    if n not in fan_numbers_with_sec_touch:
        raise cv.Invalid(
            f"select.sec_touch fan_number={n} requires a matching fan.sec_touch entry "
            f"with fan_number={n} and sec_touch_id={sec_touch_id}",
            path=[CONF_FAN_NUMBER],
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    parent = await cg.get_variable(config[CONF_SEC_TOUCH_ID])
    fan_number = config[CONF_FAN_NUMBER]
    level_id = FAN_LEVEL_IDS[fan_number - 1]

    sel = await select.new_select(
        config[CONF_MODE_SELECT],
        parent,
        level_id,
        options=MODE_OPTIONS,
    )
    await cg.register_component(sel, config[CONF_MODE_SELECT])
