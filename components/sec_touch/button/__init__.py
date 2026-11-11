import esphome.codegen as cg
from esphome.components import button, time
import esphome.config_validation as cv
from esphome.const import CONF_TIME_ID, ENTITY_CATEGORY_CONFIG


from .. import CONF_SEC_TOUCH_ID, sec_touch_ns, SECTouchComponent

DEPENDENCIES = ["sec_touch"]

ProgramTextUpdateButton = sec_touch_ns.class_("ProgramTextUpdateButton", button.Button)
CONF_PROGRAM_TEXT_UPDATE = "program_text_update"

SecTouchSyncClockButton = sec_touch_ns.class_(
    "SecTouchSyncClockButton", button.Button, cg.Component
)
CONF_SYNC_CLOCK = "sync_clock"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_SEC_TOUCH_ID): cv.use_id(SECTouchComponent),
    cv.Optional(CONF_PROGRAM_TEXT_UPDATE): button.button_schema(
        ProgramTextUpdateButton,
    ),
    cv.Optional(CONF_SYNC_CLOCK): button.button_schema(
        SecTouchSyncClockButton,
        icon="mdi:clock-check-outline",
        entity_category=ENTITY_CATEGORY_CONFIG,
    )
    .extend({cv.GenerateID(CONF_TIME_ID): cv.use_id(time.RealTimeClock)})
    .extend(cv.COMPONENT_SCHEMA),
}


async def to_code(config):
    sec_component = await cg.get_variable(config[CONF_SEC_TOUCH_ID])

    if poll_button_config := config.get(CONF_PROGRAM_TEXT_UPDATE):
        b = await button.new_button(poll_button_config)
        await cg.register_parented(b, sec_component)

    if sync_clock_config := config.get(CONF_SYNC_CLOCK):
        b = await button.new_button(sync_clock_config)
        await cg.register_component(b, sync_clock_config)
        await cg.register_parented(b, sec_component)
        time_source = await cg.get_variable(sync_clock_config[CONF_TIME_ID])
        cg.add(b.set_time(time_source))
