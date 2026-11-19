#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_fan_control.

#include <optional>

#include "fan_level.h"

namespace esphome {
namespace sec_touch {

/// What a fan call from Home Assistant asks for. Empty fields are not part of the call.
struct FanCommand {
  std::optional<bool> state;
  std::optional<int> speed;
  /// Panel level of a newly picked preset: NORMAL_PRESET_LEVEL, or BURST_LEVEL..SLEEP_LEVEL for a special mode.
  std::optional<int> preset_level;
};

/// The fan as ESPHome saw it before the call.
struct FanStatus {
  bool on;
  int speed;
};

/// What the fan does with a call.
struct FanControl {
  /// Value to write to the fan's level property; OFF_LEVEL turns the fan off.
  int level;
  /// State and speed to publish.
  bool on;
  int speed;
  /// A normal level was set without a preset: put a special preset back to Normal.
  bool reset_preset_to_normal;
};

/// Decides what a fan call writes to the panel and publishes.
inline FanControl decide_fan_control(const FanCommand &command, const FanStatus &prior, bool split_special_modes) {
  int speed = command.speed.value_or(prior.speed);

  // OFF always turns the fan off, whatever speed or preset rides along (issue #54).
  // An off fan gets OFF_LEVEL too: the panel may have it on without ESPHome knowing.
  if (command.state == false) {
    return FanControl{OFF_LEVEL, false, speed, false};
  }

  bool on = command.state.value_or(prior.on);
  bool turning_on_without_speed = command.state == true && !command.speed.has_value() && !prior.on;
  if (turning_on_without_speed && speed <= OFF_LEVEL) {
    // TODO Use a configuration https://github.com/distante/esphome-components/issues/10
    speed = MIN_NORMAL_LEVEL;
  }

  if (!command.preset_level.has_value()) {
    return FanControl{speed, on, speed, is_normal_level(speed)};
  }

  int preset_level = *command.preset_level;
  if (preset_level == NORMAL_PRESET_LEVEL) {
    // Split mode: the slider is the source of truth for continuous ventilation,
    // so keep its speed unless it sits outside the normal levels.
    if (split_special_modes && is_normal_level(speed)) {
      return FanControl{speed, on, speed, false};
    }
    return FanControl{MIN_NORMAL_LEVEL, on, MIN_NORMAL_LEVEL, false};
  }

  if (!split_special_modes) {
    return FanControl{preset_level, on, preset_level, false};
  }

  // Split mode writes the special level but keeps the slider on a normal level. Burst (Stosslüften,
  // aggressive airing-out) shows as the slider at its max. Sleep is low-intensity and the
  // Automatic_* modes vary per sensor, so neither maps to a slider position: leave it.
  int slider = (preset_level == BURST_LEVEL) ? MAX_NORMAL_LEVEL : speed;
  return FanControl{preset_level, on, slider, false};
}

}  // namespace sec_touch
}  // namespace esphome
