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

/// What a fan shows right after HA's command `decision`, from `prior`, what it showed before. A following fan that HA
/// puts in Automatik Zeit shows its running level, and keeps `prior` until it has one: showing the command would
/// show the fan on until the SET's read-back brings the running level.
inline FanStatus decide_command_display(const FanControl &decision, bool following, std::optional<int> running_level,
                                        const FanStatus &prior, bool split_special_modes) {
  if (!following || decision.level != AUTOMATIC_TIME_LEVEL) {
    return FanStatus{decision.on, decision.speed};
  }
  if (!running_level.has_value()) {
    return prior;
  }
  return decide_fan_display(AUTOMATIC_TIME_LEVEL, running_level, prior, split_special_modes);
}

/// What a fan's mode text sensor shows: these words, or the fan's preset.
enum class ModeText : uint8_t { OFF, NOT_CONNECTED, PRESET };
constexpr const char *MODE_TEXT_OFF = "Off";
constexpr const char *MODE_TEXT_NOT_CONNECTED = "Not Connected";
/// A preset the fan doesn't have yet.
constexpr const char *MODE_TEXT_UNKNOWN = "Unknown";

/// The mode text for `shown`, what the fan shows for `device_level`, its pair's level (nullopt before a good read).
/// A fan its time program stops still names its preset: the pair is in Automatik Zeit, not off.
inline ModeText decide_mode_text(std::optional<int> device_level, const FanStatus &shown) {
  if (!shown.on && device_level != AUTOMATIC_TIME_LEVEL) {
    return ModeText::OFF;
  }
  if (shown.speed == NOT_CONNECTED_LEVEL) {
    return ModeText::NOT_CONNECTED;
  }
  return ModeText::PRESET;
}

/// Whether a following fan holds its first display after boot: its pair is in Automatik Zeit and its live state has
/// no state yet, so it would show Automatik Zeit's display without a running level and change it seconds later.
inline bool holds_first_display(bool following, bool shown_before, int device_level, bool has_live_state) {
  return following && !shown_before && device_level == AUTOMATIC_TIME_LEVEL && !has_live_state;
}

}  // namespace sec_touch
}  // namespace esphome
