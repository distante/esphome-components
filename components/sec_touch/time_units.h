#pragma once

// Plain C++, no ESPHome includes: the units of time the components convert between.

#include <cstdint>

namespace esphome {
namespace sec_touch {

constexpr uint32_t MS_PER_S = 1000;
constexpr int SECONDS_PER_MINUTE = 60;
constexpr int MINUTES_PER_HOUR = 60;
constexpr int HOURS_PER_DAY = 24;
constexpr int SECONDS_PER_DAY = HOURS_PER_DAY * MINUTES_PER_HOUR * SECONDS_PER_MINUTE;
constexpr uint32_t MS_PER_MINUTE = SECONDS_PER_MINUTE * MS_PER_S;
constexpr uint32_t MS_PER_HOUR = MINUTES_PER_HOUR * MS_PER_MINUTE;

/// Whether millis() reading `now_ms` has reached `at_ms`. A signed difference, so it stays right across the millis()
/// wrap for any wait below half the range (about 24.8 days).
constexpr bool millis_reached(uint32_t now_ms, uint32_t at_ms) { return static_cast<int32_t>(now_ms - at_ms) >= 0; }

}  // namespace sec_touch
}  // namespace esphome
