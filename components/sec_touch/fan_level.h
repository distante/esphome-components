#pragma once

// Plain C++, no ESPHome includes: shared by the fan component, FanModeEnum, protocol_ids.h and tests/cpp. It sits in
// the component root, not in fan/, because ESPHome copies fan/ only into builds that load the fan platform.

namespace esphome {
namespace sec_touch {

// Values of a fan's level property. The SEC-Touch protocol fixes them.

/// Turns the fan off. The Normal preset's range starts here too.
constexpr int OFF_LEVEL = 0;
/// Level a Normal preset carries: the start of its range, OFF_LEVEL..MAX_NORMAL_LEVEL.
constexpr int NORMAL_PRESET_LEVEL = OFF_LEVEL;
/// Continuous ventilation runs at MIN_NORMAL_LEVEL..MAX_NORMAL_LEVEL; split mode's slider covers the same range.
constexpr int MIN_NORMAL_LEVEL = 1;
constexpr int MAX_NORMAL_LEVEL = 6;
/// Special modes, each one a single level.
constexpr int BURST_LEVEL = 7;
constexpr int AUTOMATIC_HUMIDITY_LEVEL = 8;
constexpr int AUTOMATIC_CO2_LEVEL = 9;
constexpr int AUTOMATIC_TIME_LEVEL = 10;
constexpr int SLEEP_LEVEL = 11;
/// Highest level the panel reports for a running fan.
constexpr int MAX_LEVEL = SLEEP_LEVEL;
/// Level the panel reports when no fan is connected to the slot.
constexpr int NOT_CONNECTED_LEVEL = 255;

inline bool is_normal_level(int level) { return level >= MIN_NORMAL_LEVEL && level <= MAX_NORMAL_LEVEL; }

}  // namespace sec_touch
}  // namespace esphome
