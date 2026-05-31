from esphome.components import switch
import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import CONF_OUTPUT_ID
from .. import (
    CONF_SEC_TOUCH_ID,
    sec_touch_ns,
    SECTouchComponent,
)

DEPENDENCIES = ["sec_touch"]

SecTouchSummerVentilationSwitch = sec_touch_ns.class_(
    "SecTouchSummerVentilationSwitch", cg.Component, switch.Switch
)

CONFIG_SCHEMA = cv.All(
    switch.switch_schema(SecTouchSummerVentilationSwitch)
    .extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(
                SecTouchSummerVentilationSwitch
            ),
            cv.GenerateID(CONF_SEC_TOUCH_ID): cv.use_id(SECTouchComponent),
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_SEC_TOUCH_ID])
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID], parent)
    await cg.register_component(var, config)
    await switch.register_switch(var, config)
