#pragma once

#include <optional>

#include "esphome/core/component.h"
#include "esphome/components/fan/fan.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "../sec_touch.h"
#include "_fan_mode.h"
#include "fan_control.h"
#include "fan_display.h"
#include "../fan_level.h"
#include "../time_program_live_state.h"

namespace esphome {
namespace sec_touch {

class SecTouchFan : public Component, public fan::Fan {
  static const char *const TAG;

 protected:
  const int level_id;
  const int label_id;
  SECTouchComponent *parent;
  bool split_special_modes_{false};
  void control(const fan::FanCall &call) override;
  static FanModeEnum::FanMode calculate_mode_from_speed(int speed);
  void update_label_mode();
  void on_live_state_(const std::optional<LiveState> &state);
  /// Shows the last level read, with the running level when the fan follows it; publishes only a change.
  void show_device_level_();

  /// The pair's level as last read; nullopt until a good read.
  std::optional<int> device_level_;
  /// The pair's running level while the fan follows it and the pair has one.
  std::optional<int> running_level_;

 public:
  SecTouchFan(SECTouchComponent *parent, int level_id, int label_id);

  // When true, the HA fan exposes only speeds 1..6 (continuous ventilation).
  // Levels 7..11 (Burst, Automatic Humidity/CO2/Time, Sleep) remain reachable via preset_mode.
  void set_split_special_modes(bool v) { this->split_special_modes_ = v; }
  /// In Automatik Zeit, show the pair's running level (.specs/time-programs.md, "Live state").
  void follow_time_program(TimeProgramLiveState *live_state);

  void setup() override { this->set_supported_preset_modes(FanModeEnum::getPresetModePointers()); }
  fan::FanTraits get_traits() override {
    auto traits = fan::FanTraits(false, true, false, this->split_special_modes_ ? MAX_NORMAL_LEVEL : MAX_LEVEL);
    this->wire_preset_modes_(traits);
    return traits;
  }

  void dump_config() override;
};

}  // namespace sec_touch
}  // namespace esphome
