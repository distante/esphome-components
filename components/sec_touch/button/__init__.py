import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv


from .. import CONF_SEC_TOUCH_ID, sec_touch_ns, SECTouchComponent

DEPENDENCIES = ["sec_touch"]

ProgramTextUpdateButton = sec_touch_ns.class_("ProgramTextUpdateButton", button.Button)
CONF_PROGRAM_TEXT_UPDATE = "program_text_update"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_SEC_TOUCH_ID): cv.use_id(SECTouchComponent),
    cv.Optional(CONF_PROGRAM_TEXT_UPDATE): button.button_schema(
        ProgramTextUpdateButton,
    ),
}


async def to_code(config):
    sec_component = await cg.get_variable(config[CONF_SEC_TOUCH_ID])

    if poll_button_config := config.get(CONF_PROGRAM_TEXT_UPDATE):
        b = await button.new_button(poll_button_config)
        await cg.register_parented(b, sec_component)
