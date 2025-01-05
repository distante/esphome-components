import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv


from .. import CONF_SEC_TOUCH_ID, sec_touch_ns, SECTouchComponent

DEPENDENCIES = ["sec_touch"]

UpdateNowButton = sec_touch_ns.class_("UpdateNowButton", button.Button)
CONF_UPDATE_NOW_BUTTON = "update_now"

FillQueueButton = sec_touch_ns.class_("FillQueueButton", button.Button)
CONF_FILL_QUEUE_BUTTON = "fill_queue"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_SEC_TOUCH_ID): cv.use_id(SECTouchComponent),
    cv.Optional(CONF_UPDATE_NOW_BUTTON): button.button_schema(
        UpdateNowButton,
    ),
    cv.Optional(CONF_FILL_QUEUE_BUTTON): button.button_schema(
        FillQueueButton,
    ),
}


async def to_code(config):
    sec_component = await cg.get_variable(config[CONF_SEC_TOUCH_ID])

    if poll_button_config := config.get(CONF_UPDATE_NOW_BUTTON):
        b = await button.new_button(poll_button_config)
        await cg.register_parented(b, sec_component)

    if fill_queue_button_config := config.get(CONF_FILL_QUEUE_BUTTON):
        b = await button.new_button(fill_queue_button_config)
        await cg.register_parented(b, sec_component)
