import esphome.codegen as cg
from esphome.components import sensor, time
import esphome.config_validation as cv
from esphome.const import (
    CONF_TIME_ID,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_SECOND,
)

from .. import CONF_SEC_TOUCH_ID, SECTouchComponent, sec_touch_ns

DEPENDENCIES = ["sec_touch", "time"]

SecTouchClockDriftSensor = sec_touch_ns.class_(
    "SecTouchClockDriftSensor", sensor.Sensor, cg.Component
)
CONF_CLOCK_DRIFT = "clock_drift"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_SEC_TOUCH_ID): cv.use_id(SECTouchComponent),
    cv.Optional(CONF_CLOCK_DRIFT): sensor.sensor_schema(
        SecTouchClockDriftSensor,
        unit_of_measurement=UNIT_SECOND,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        icon="mdi:clock-alert-outline",
    )
    .extend({cv.GenerateID(CONF_TIME_ID): cv.use_id(time.RealTimeClock)})
    .extend(cv.COMPONENT_SCHEMA),
}


async def to_code(config):
    sec_component = await cg.get_variable(config[CONF_SEC_TOUCH_ID])

    if drift_config := config.get(CONF_CLOCK_DRIFT):
        s = await sensor.new_sensor(drift_config)
        await cg.register_component(s, drift_config)
        await cg.register_parented(s, sec_component)
        time_source = await cg.get_variable(drift_config[CONF_TIME_ID])
        cg.add(s.set_time(time_source))
