from pathlib import Path

from esphome import automation
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import (
    button as esphome_button,
    datetime,
    event,
    select as esphome_select,
    switch as esphome_switch,
    text_sensor as esphome_text_sensor,
    time,
    uart,
)
from esphome.const import (
    CONF_DEVICE_ID,
    CONF_ENTITY_CATEGORY,
    CONF_ID,
    CONF_LEVEL,
    CONF_NAME,
    CONF_ON_ERROR,
    CONF_ON_SUCCESS,
    CONF_REFRESH,
    CONF_TIME,
    CONF_TIME_ID,
    CONF_TIMES,
    CONF_WEB_SERVER,
    DEVICE_CLASS_TIMESTAMP,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from esphome.core import CORE
from esphome.core.entity_helpers import WEBSERVER_SORTING_SCHEMA

from .header_reader import HeaderReader

DEPENDENCIES = ["uart"]

# The hub's config key and its platforms' `platform:` value.
DOMAIN = "sec_touch"

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
CONF_FAN_NUMBER = "fan_number"

# Time programs (.specs/time-programs.md, "Configuration").
CONF_TIME_PROGRAMS = "time_programs"
CONF_TIME_PROGRAMS_UPDATE_INTERVAL = "time_programs_update_interval"
CONF_SWITCH_TIMES = "switch_times"
CONF_WRITE_FAILED = "write_failed"
# Generated from switch_times before validation, never written by the user: one item per listed switch time.
CONF_FIELD_ENTITIES = "field_entities"
CONF_SWITCH_TIME = "switch_time"
CONF_ENABLED = "enabled"
# Live state (.specs/time-programs.md, "Live state").
CONF_SWITCH_TIME_IN_EFFECT = "switch_time_in_effect"
CONF_RUNNING_LEVEL = "running_level"
CONF_NEXT_LEVEL = "next_level"
CONF_NEXT_CHANGE = "next_change"
LIVE_STATE_TEXT_KEYS = (CONF_SWITCH_TIME_IN_EFFECT, CONF_RUNNING_LEVEL, CONF_NEXT_LEVEL)
LIVE_STATE_KEYS = LIVE_STATE_TEXT_KEYS + (CONF_NEXT_CHANGE,)
# On a sec_touch fan entry: in Automatik Zeit, show the pair's running level.
CONF_FOLLOW_TIME_PROGRAM = "follow_time_program"
# Generated before validation, never written by the user: _prepare_live_states.
CONF_LIVE_STATES = "live_states"

FAN_PAIR_COUNT = len(FAN_LEVEL_IDS)


def _protocol_ids() -> HeaderReader:
    """protocol_ids.h, read at config time: the C++ header stays the one source of the values taken from it."""
    reader = HeaderReader()
    reader.read(Path(__file__).parent / "protocol_ids.h")
    return reader


_PROTOCOL_IDS = _protocol_ids()
SWITCH_TIME_COUNT = _PROTOCOL_IDS.get(_PROTOCOL_IDS.ints, "SWITCH_TIME_COUNT")

TimeProgram = sec_touch_ns.class_("TimeProgram")
TimeProgramTime = sec_touch_ns.class_("TimeProgramTime", datetime.TimeEntity)
TimeProgramLevelSelect = sec_touch_ns.class_("TimeProgramLevelSelect", esphome_select.Select)
TimeProgramEnabledSwitch = sec_touch_ns.class_(
    "TimeProgramEnabledSwitch", esphome_switch.Switch
)
TimeProgramRefreshButton = sec_touch_ns.class_(
    "TimeProgramRefreshButton", esphome_button.Button
)
TimeProgramWriteFailedEvent = sec_touch_ns.class_(
    "TimeProgramWriteFailedEvent", event.Event
)
TimeProgramLiveState = sec_touch_ns.class_("TimeProgramLiveState", cg.Component)

# The domains each time-program entity needs.
TIME_PROGRAM_DOMAINS = ["datetime", "select", "switch"]
REFRESH_DOMAIN = "button"
WRITE_FAILED_DOMAIN = "event"


# A config value read as ESPHome reads a list key: one entry becomes a list of it, None and {} an empty list. For the
# raw config, before the schema's own cv.ensure_list runs.
_as_list = cv.ensure_list()


def AUTO_LOAD(config):
    # The hub's public interface uses text_sensor::TextSensor, so it must be in
    # the build even when the config has no text_sensor: section.
    base = ["text_sensor"]
    # A falsy config is a tooling probe for the maximal set (None from
    # dependency resolution, {} from the components-graph platform probe).
    if not config:
        return (
            base
            + TIME_PROGRAM_DOMAINS
            + [REFRESH_DOMAIN, WRITE_FAILED_DOMAIN]
        )
    entries = _as_list(config.get(CONF_TIME_PROGRAMS))
    if not entries:
        return base
    loads = base + TIME_PROGRAM_DOMAINS
    if any(CONF_REFRESH in entry for entry in entries):
        loads.append(REFRESH_DOMAIN)
    if any(CONF_WRITE_FAILED in entry for entry in entries):
        loads.append(WRITE_FAILED_DOMAIN)
    return loads


FAN_NUMBER_SCHEMA = cv.int_range(min=1, max=FAN_PAIR_COUNT)
FAN_PAIR_SCHEMA = cv.Schema({cv.Required(CONF_FAN_NUMBER): FAN_NUMBER_SCHEMA})


def unique(what):
    def validator(values):
        seen = set()
        for value in values:
            if value in seen:
                raise cv.Invalid(f"{what} {value} is listed twice")
            seen.add(value)
        return values

    return validator


SWITCH_TIMES_SCHEMA = cv.All(
    cv.ensure_list(cv.int_range(min=1, max=SWITCH_TIME_COUNT)),
    unique("switch time"),
)

FIELD_ENTITIES_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SWITCH_TIME): cv.int_range(min=1, max=SWITCH_TIME_COUNT),
        cv.Required(CONF_TIME): datetime.time_schema(TimeProgramTime),
        cv.Required(CONF_LEVEL): esphome_select.select_schema(TimeProgramLevelSelect),
        cv.Required(CONF_ENABLED): esphome_switch.switch_schema(TimeProgramEnabledSwitch),
    }
)

TIME_PROGRAM_SCHEMA = FAN_PAIR_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(TimeProgram),
        cv.Optional(CONF_NAME): cv.string,
        cv.Optional(CONF_DEVICE_ID): cv.sub_device_id,
        cv.Optional(
            CONF_SWITCH_TIMES, default=list(range(1, SWITCH_TIME_COUNT + 1))
        ): SWITCH_TIMES_SCHEMA,
        cv.Optional(CONF_REFRESH): esphome_button.button_schema(
            TimeProgramRefreshButton, entity_category=ENTITY_CATEGORY_CONFIG
        ),
        cv.Optional(CONF_WRITE_FAILED): event.event_schema(
            TimeProgramWriteFailedEvent, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        cv.Required(CONF_FIELD_ENTITIES): cv.ensure_list(FIELD_ENTITIES_SCHEMA),
        **{
            cv.Optional(key): esphome_text_sensor.text_sensor_schema()
            for key in LIVE_STATE_TEXT_KEYS
        },
        cv.Optional(CONF_NEXT_CHANGE): esphome_text_sensor.text_sensor_schema(
            device_class=DEVICE_CLASS_TIMESTAMP
        ).extend({cv.GenerateID(CONF_TIME_ID): cv.use_id(time.RealTimeClock)}),
    }
).extend(WEBSERVER_SORTING_SCHEMA)

# The entries a pair can have besides its field entities; their own device_id and web_server win over the pair's.
OPTIONAL_ENTITY_KEYS = (CONF_REFRESH, CONF_WRITE_FAILED) + LIVE_STATE_KEYS


def sec_touch_fans(fans):
    """The sec_touch entries of a `fan:` list, raw or validated."""
    return [
        fan
        for fan in _as_list(fans)
        if isinstance(fan, dict) and fan.get("platform") == DOMAIN
    ]


def _raw_fan_number(fan):
    """A raw `fan:` entry's fan_number as its schema reads it (a quoted "1" is 1), or None when the schema rejects it."""
    try:
        return FAN_NUMBER_SCHEMA(fan.get(CONF_FAN_NUMBER))
    except cv.Invalid:
        return None


def _fan_entries(hub_config, fan_number):
    """This hub's `fan:` entries for `fan_number`."""
    hub_id = hub_config.get(CONF_ID)
    for fan in sec_touch_fans((CORE.raw_config or {}).get("fan")):
        if _raw_fan_number(fan) != fan_number:
            continue
        if fan.get(CONF_SEC_TOUCH_ID) not in (None, hub_id):
            continue
        yield fan


def _fan_entry_name(hub_config, fan_number):
    """The name of this hub's `fan:` entry for `fan_number`, or None."""
    for fan in _fan_entries(hub_config, fan_number):
        if fan.get(CONF_NAME):
            return fan[CONF_NAME]
    return None


def _prepare_time_program(hub_config, entry):
    """Builds a pair's field entities and gives each entity the pair's device_id and web_server, before any entity
    schema runs, so ESPHome's per-device duplicate-name check sees the final names and devices (.specs/time-programs.md,
    "Sub-device"), and each entity's own schema validates its web_server block."""
    if not isinstance(entry, dict):
        raise cv.Invalid("expected a dictionary")
    if CONF_FIELD_ENTITIES in entry:
        raise cv.Invalid(
            f"{CONF_FIELD_ENTITIES} is generated from {CONF_SWITCH_TIMES}",
            path=[CONF_FIELD_ENTITIES],
        )
    try:
        fan_number = FAN_NUMBER_SCHEMA(entry.get(CONF_FAN_NUMBER))
    except cv.Invalid as err:
        err.prepend([CONF_FAN_NUMBER])
        raise
    try:
        switch_times = SWITCH_TIMES_SCHEMA(
            entry.get(CONF_SWITCH_TIMES, list(range(1, SWITCH_TIME_COUNT + 1)))
        )
    except cv.Invalid as err:
        err.prepend([CONF_SWITCH_TIMES])
        raise

    entry = dict(entry)
    live_state = [key for key in LIVE_STATE_KEYS if key in entry]
    if live_state:
        # Live state reads the pair's level ID, which only the pair's fan entry polls.
        if next(_fan_entries(hub_config, fan_number), None) is None:
            raise cv.Invalid(
                f"{live_state[0]} needs this hub's {DOMAIN} fan entry with {CONF_FAN_NUMBER} {fan_number}",
                path=[live_state[0]],
            )
    device_id = entry.get(CONF_DEVICE_ID)
    prefix = ""
    if device_id is None:
        pair_name = (
            entry.get(CONF_NAME)
            or _fan_entry_name(hub_config, fan_number)
            or f"Fan pair {fan_number}"
        )
        prefix = f"{pair_name} "

    # Keys an entity takes from the pair: in the field entities always, in an optional entity unless it sets its own.
    # A web_server that isn't a dictionary stays out, so the pair's schema reports it once, not each entity again.
    # A dictionary with a bad value is copied, so each entity reports it too.
    inherited = {}
    if CONF_DEVICE_ID in entry:
        inherited[CONF_DEVICE_ID] = entry[CONF_DEVICE_ID]
    if isinstance(entry.get(CONF_WEB_SERVER), dict):
        inherited[CONF_WEB_SERVER] = entry[CONF_WEB_SERVER]

    def field(n, kind):
        return {
            CONF_NAME: f"{prefix}Switch time {n} {kind}",
            CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_CONFIG,
            **inherited,
        }

    entry[CONF_FIELD_ENTITIES] = [
        {
            CONF_SWITCH_TIME: n,
            CONF_TIME: field(n, CONF_TIME),
            CONF_LEVEL: field(n, CONF_LEVEL),
            CONF_ENABLED: field(n, CONF_ENABLED),
        }
        for n in switch_times
    ]
    for key in OPTIONAL_ENTITY_KEYS:
        optional_entity = entry.get(key)
        # A missing entry stays missing; one that isn't a dictionary is the entity schema's to report.
        if not inherited or not isinstance(optional_entity, dict):
            continue
        entry[key] = {**inherited, **optional_entity}
    return entry


def _follows(fan):
    """Whether a raw `fan:` entry follows its pair's running level. A value its schema rejects counts as not
    following: the fan's schema reports it."""
    try:
        return cv.boolean(fan.get(CONF_FOLLOW_TIME_PROGRAM, True))
    except cv.Invalid:
        return False


def _prepare_live_states(hub_config):
    """Lists the pairs that get a live state, shared by the pair's fan and its live-state entities: those whose fan
    follows and those with a live-state entity."""
    if not isinstance(hub_config, dict):
        return hub_config
    if CONF_LIVE_STATES in hub_config:
        raise cv.Invalid(f"{CONF_LIVE_STATES} is generated", path=[CONF_LIVE_STATES])
    # _prepare_time_programs checked each fan_number; a quoted one is still a string.
    with_entity = {
        FAN_NUMBER_SCHEMA(entry[CONF_FAN_NUMBER])
        for entry in hub_config.get(CONF_TIME_PROGRAMS, [])
        if any(key in entry for key in LIVE_STATE_KEYS)
    }
    pairs = [
        fan_number
        for fan_number in range(1, FAN_PAIR_COUNT + 1)
        if fan_number in with_entity or any(_follows(fan) for fan in _fan_entries(hub_config, fan_number))
    ]
    if not pairs:
        return hub_config
    return {**hub_config, CONF_LIVE_STATES: [{CONF_FAN_NUMBER: fan_number} for fan_number in pairs]}


def _prepare_time_programs(hub_config):
    if not isinstance(hub_config, dict) or CONF_TIME_PROGRAMS not in hub_config:
        return hub_config
    entries = _as_list(hub_config[CONF_TIME_PROGRAMS])
    prepared = []
    seen = set()
    for i, entry in enumerate(entries):
        try:
            entry = _prepare_time_program(hub_config, entry)
        except cv.Invalid as err:
            err.prepend([CONF_TIME_PROGRAMS, i])
            raise
        fan_number = entry[CONF_FAN_NUMBER]
        if fan_number in seen:
            raise cv.Invalid(
                f"{CONF_FAN_NUMBER} {fan_number} is listed twice in {CONF_TIME_PROGRAMS}",
                path=[CONF_TIME_PROGRAMS, i, CONF_FAN_NUMBER],
            )
        seen.add(fan_number)
        prepared.append(entry)
    return {**hub_config, CONF_TIME_PROGRAMS: prepared}


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SECTouchComponent),
        cv.Optional(CONF_INTER_TASK_DELAY, default=30): cv.positive_int,
        cv.Optional(
            CONF_GLOBAL_SETTINGS_UPDATE_INTERVAL, default="30s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_OPTIONAL_CRC, default=False): cv.boolean,
        # Positive: a zero interval would poll every idle loop.
        cv.Optional(CONF_TIME_PROGRAMS_UPDATE_INTERVAL, default="5min"): cv.All(
            cv.positive_not_null_time_period, cv.time_period_in_milliseconds_
        ),
        cv.Optional(CONF_TIME_PROGRAMS): cv.ensure_list(TIME_PROGRAM_SCHEMA),
        cv.Optional(CONF_LIVE_STATES): cv.ensure_list(
            FAN_PAIR_SCHEMA.extend({cv.GenerateID(): cv.declare_id(TimeProgramLiveState)})
        ),
    }
)


CONFIG_SCHEMA = cv.All(
    _prepare_time_programs,
    _prepare_live_states,
    CONFIG_SCHEMA.extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
    .extend(cv.polling_component_schema("5s")),
)

# sec_touch.set_time_program (.specs/time-programs.md, "Bulk-set action").
CONF_LEVELS = "levels"
SetTimeProgramAction = sec_touch_ns.class_(
    "SetTimeProgramAction",
    automation.Action,
    cg.Parented.template(SECTouchComponent),
)
# Where each set_time_program records its hub and constant fan_number (None for a lambda), so the hub's final
# validation can check them against its time_programs: an action may be validated before the hub.
SET_TIME_PROGRAM_CALLS = "sec_touch_set_time_program_calls"


def _record_set_time_program_call(config):
    fan_number = config[CONF_FAN_NUMBER]
    CORE.data.setdefault(SET_TIME_PROGRAM_CALLS, []).append(
        (config[CONF_ID], None if cg.is_template(fan_number) else fan_number)
    )
    return config


# Only the list shapes are checked here; C++ checks every value at run time, with one rule for constants and
# lambdas alike (time_program_bulk.h).
SET_TIME_PROGRAM_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(SECTouchComponent),
            cv.Required(CONF_FAN_NUMBER): cv.templatable(FAN_NUMBER_SCHEMA),
            cv.Required(CONF_TIMES): cv.templatable(cv.ensure_list(cv.string_strict)),
            cv.Required(CONF_LEVELS): cv.templatable(cv.ensure_list(cv.int_)),
            cv.Required(CONF_ENABLED): cv.templatable(cv.ensure_list(cv.boolean)),
            cv.Optional(CONF_ON_SUCCESS): automation.validate_automation(single=True),
            cv.Optional(CONF_ON_ERROR): automation.validate_automation(single=True),
        }
    ),
    _record_set_time_program_call,
)


def _final_validate_set_time_program_calls(config):
    listed = {entry[CONF_FAN_NUMBER] for entry in config.get(CONF_TIME_PROGRAMS, [])}
    for hub_id, fan_number in CORE.data.get(SET_TIME_PROGRAM_CALLS, []):
        if hub_id != config[CONF_ID]:
            continue
        if not listed:
            raise cv.Invalid(
                f"sec_touch.set_time_program needs at least one entry in {DOMAIN} {CONF_TIME_PROGRAMS}"
            )
        if fan_number is not None and fan_number not in listed:
            raise cv.Invalid(
                f"sec_touch.set_time_program: {CONF_FAN_NUMBER} {fan_number} is not listed in {DOMAIN} {CONF_TIME_PROGRAMS}"
            )
    return config


FINAL_VALIDATE_SCHEMA = cv.All(
    uart.final_validate_device_schema(
        DOMAIN,
        require_tx=True,
        require_rx=True,
        # parity="NONE",
        # stop_bits=1,
        baud_rate=28800,
    ),
    _final_validate_set_time_program_calls,
)


async def _bulk_argument(value, args, value_type):
    """An argument for BulkArgument (time_program_bulk.h): the constant, or a lambda that takes the call's
    arguments by const reference, so reading the arguments copies none of the call's lists."""
    if not cg.is_template(value):
        return cg.RawExpression(f"{value_type}({cg.safe_exp(value)})")
    by_reference = [(cg.RawExpression(f"const {arg_type} &"), name) for arg_type, name in args]
    return await cg.process_lambda(value, by_reference, return_type=value_type)


# synchronous=False: on_success / on_error fire after the writes end, with the call's arguments, so those must be
# owning copies (the API then passes std::vector, not views into its receive buffer).
@automation.register_action(
    "sec_touch.set_time_program",
    SetTimeProgramAction,
    SET_TIME_PROGRAM_SCHEMA,
    synchronous=False,
)
async def set_time_program_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_fan_number(await _bulk_argument(config[CONF_FAN_NUMBER], args, cg.int_)))
    cg.add(
        var.set_times(
            await _bulk_argument(
                config[CONF_TIMES], args, cg.std_vector.template(cg.std_string)
            )
        )
    )
    cg.add(
        var.set_levels(
            await _bulk_argument(
                config[CONF_LEVELS], args, cg.std_vector.template(cg.int32)
            )
        )
    )
    cg.add(
        var.set_enabled(
            await _bulk_argument(
                config[CONF_ENABLED], args, cg.std_vector.template(cg.bool_)
            )
        )
    )
    if on_success := config.get(CONF_ON_SUCCESS):
        await automation.build_automation(
            var.get_success_trigger(), args, on_success
        )
    # `error` comes after the call's arguments: api.respond reads call_id from the first one.
    if on_error := config.get(CONF_ON_ERROR):
        await automation.build_automation(
            var.get_error_trigger(), [*args, (cg.std_string, "error")], on_error
        )
    return var


async def _time_program_to_code(hub, entry):
    program = cg.new_Pvariable(entry[CONF_ID], hub, entry[CONF_FAN_NUMBER])
    for fields in entry[CONF_FIELD_ENTITIES]:
        n = fields[CONF_SWITCH_TIME]
        await datetime.new_datetime(fields[CONF_TIME], program, n)
        # The options come from C++ (SWITCH_TIME_LEVEL_OPTIONS), the one list of level names.
        await esphome_select.new_select(fields[CONF_LEVEL], program, n, options=[])
        await esphome_switch.new_switch(fields[CONF_ENABLED], program, n)
    if refresh := entry.get(CONF_REFRESH):
        await esphome_button.new_button(refresh, program)
    if write_failed := entry.get(CONF_WRITE_FAILED):
        # The event types come from C++ (WRITE_FAILURE_NAMES).
        failed = cg.new_Pvariable(write_failed[CONF_ID], program)
        await event.register_event(failed, write_failed, event_types=[])
    if any(key in entry for key in LIVE_STATE_KEYS):
        await _live_state_entities_to_code(entry)


def live_state_id(fan_number):
    """The ID of pair `fan_number`'s live state, from the validated hub config."""
    for entry in CORE.config[DOMAIN].get(CONF_LIVE_STATES, []):
        if entry[CONF_FAN_NUMBER] == fan_number:
            return entry[CONF_ID]
    raise ValueError(f"fan pair {fan_number} has no live state")


async def _live_state_entities_to_code(entry):
    live_state = await cg.get_variable(live_state_id(entry[CONF_FAN_NUMBER]))
    if conf := entry.get(CONF_SWITCH_TIME_IN_EFFECT):
        sens = await esphome_text_sensor.new_text_sensor(conf)
        cg.add(live_state.set_switch_time_in_effect(sens))
    if conf := entry.get(CONF_RUNNING_LEVEL):
        sens = await esphome_text_sensor.new_text_sensor(conf)
        cg.add(live_state.set_running_level(sens))
    if conf := entry.get(CONF_NEXT_LEVEL):
        sens = await esphome_text_sensor.new_text_sensor(conf)
        cg.add(live_state.set_next_level(sens))
    if conf := entry.get(CONF_NEXT_CHANGE):
        sens = await esphome_text_sensor.new_text_sensor(conf)
        time_source = await cg.get_variable(conf[CONF_TIME_ID])
        cg.add(live_state.set_next_change(sens, time_source))


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
    cg.add(
        var.set_time_programs_update_interval_ms(
            config[CONF_TIME_PROGRAMS_UPDATE_INTERVAL]
        )
    )
    for entry in config.get(CONF_LIVE_STATES, []):
        live_state = cg.new_Pvariable(entry[CONF_ID], var, entry[CONF_FAN_NUMBER])
        await cg.register_component(live_state, {})
    for entry in config.get(CONF_TIME_PROGRAMS, []):
        await _time_program_to_code(var, entry)
