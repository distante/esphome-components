#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_fan_control.

#include <optional>

namespace esphome {
namespace sec_touch {

/// Panel level of the Burst preset; split mode shows it as the slider at its max.
constexpr int BURST_LEVEL = 7;
/// Highest level of continuous ventilation, and the slider's max in split mode.
constexpr int MAX_NORMAL_LEVEL = 6;

/// What a fan call from Home Assistant asks for. Empty fields are not part of the call.
struct FanCommand {
  std::optional<bool> state;
  std::optional<int> speed;
  /// Panel level of a newly picked preset: 0 for Normal, 7..11 for the special modes.
  std::optional<int> preset_level;
};

/// The fan as ESPHome saw it before the call.
struct FanStatus {
  bool on;
  int speed;
};

/// What the fan does with a call.
struct FanControl {
  /// Value to write to the fan's level property; 0 turns the fan off.
  int level;
  /// State and speed to publish.
  bool on;
  int speed;
  /// A normal level was set without a preset, so the preset should read Normal again.
  bool preset_is_normal;
};

inline bool is_normal_level(int level) { return level >= 1 && level <= MAX_NORMAL_LEVEL; }

/// Decides what a fan call writes to the panel and publishes.
inline FanControl decide_fan_control(const FanCommand &command, const FanStatus &prior, bool split_special_modes) {
  int speed = command.speed.value_or(prior.speed);

  // OFF always turns the fan off, whatever speed or preset rides along (issue #54).
  // An off fan gets level 0 too: the panel may have it on without ESPHome knowing.
  if (command.state == false) {
    return FanControl{0, false, speed, false};
  }

  bool on = command.state.value_or(prior.on);
  bool turning_on_without_speed = command.state == true && !command.speed.has_value() && !prior.on;
  if (turning_on_without_speed && speed <= 0) {
    // TODO Use a configuration https://github.com/distante/esphome-components/issues/10
    speed = 1;
  }

  if (!command.preset_level.has_value()) {
    return FanControl{speed, on, speed, is_normal_level(speed)};
  }

  int preset_level = *command.preset_level;
  if (preset_level == 0) {
    // Normal. Split mode: the slider is the source of truth for continuous ventilation,
    // so keep its speed unless it sits outside 1..6.
    if (split_special_modes && is_normal_level(speed)) {
      return FanControl{speed, on, speed, false};
    }
    return FanControl{1, on, 1, false};
  }

  if (!split_special_modes) {
    return FanControl{preset_level, on, preset_level, false};
  }

  // Split mode writes the special level but keeps the slider in 1..6. Burst (Stosslüften,
  // aggressive airing-out) shows as the slider at its max. Sleep is low-intensity and the
  // Automatic_* modes vary per sensor, so neither maps to a slider position: leave it.
  int slider = (preset_level == BURST_LEVEL) ? MAX_NORMAL_LEVEL : speed;
  return FanControl{preset_level, on, slider, false};
}

}  // namespace sec_touch
}  // namespace esphome
