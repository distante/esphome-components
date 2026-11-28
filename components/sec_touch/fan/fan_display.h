#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_fan_display.

#include <optional>

#include "../fan_level.h"
#include "fan_control.h"

namespace esphome {
namespace sec_touch {

/// What the fan shows for its pair's level, `device_level` (OFF_LEVEL..MAX_LEVEL or NOT_CONNECTED_LEVEL), from
/// `prior`, what it shows now. `running_level` is the pair's running level for a fan that follows it, and nullopt
/// for one that doesn't or while the pair has none to follow (.specs/time-programs.md, "Live state").
inline FanStatus decide_fan_display(int device_level, std::optional<int> running_level, const FanStatus &prior,
                                    bool split_special_modes) {
  if (device_level == AUTOMATIC_TIME_LEVEL && running_level.has_value()) {
    if (*running_level == OFF_LEVEL) {
      return FanStatus{false, prior.speed};
    }
    if (is_normal_level(*running_level)) {
      return FanStatus{true, *running_level};
    }
  }

  if (device_level == OFF_LEVEL) {
    return FanStatus{false, prior.speed};
  }
  if (device_level == NOT_CONNECTED_LEVEL) {
    return FanStatus{false, NOT_CONNECTED_LEVEL};
  }
  if (device_level <= MAX_NORMAL_LEVEL || !split_special_modes) {
    return FanStatus{true, device_level};
  }

  // Split mode: Burst snaps the slider to its max for visual parity with the outbound control() path; other special
  // modes don't map to a single level and leave it. ON always comes with a normal slider value: after boot (speed 0)
  // or "not connected" the speed sits outside the advertised range.
  if (device_level == BURST_LEVEL) {
    return FanStatus{true, MAX_NORMAL_LEVEL};
  }
  return FanStatus{true, is_normal_level(prior.speed) ? prior.speed : MIN_NORMAL_LEVEL};
}

}  // namespace sec_touch
}  // namespace esphome
