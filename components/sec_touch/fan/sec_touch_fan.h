#pragma once

#include "esphome/core/component.h"
#include "esphome/components/fan/fan.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "../sec_touch.h"
#include "_fan_mode.h"

namespace esphome {
namespace sec_touch {

class SecTouchFan : public Component, public fan::Fan {
  static constexpr const char *TAG = "SecTouchFan";

 protected:
  const int level_id;
  const int label_id;
  SECTouchComponent *parent;
  void control(const fan::FanCall &call) override;
  static FanModeEnum::FanMode calculate_mode_from_speed(int speed);
  void update_label_mode();
  fan::FanTraits traits_;
  /**
   * Will return `true` if an assignment was done so a call to publish is needed.
   */
  bool assign_new_speed_if_needed(int real_speed_from_device);

 public:
  SecTouchFan(SECTouchComponent *parent, int level_id, int label_id);

  void setup() override {
    this->traits_ = fan::FanTraits(false, true, false, 11);
    std::set<std::string> preset_modes;
    for (auto str_view : FanModeEnum::getStringValues()) {
      preset_modes.insert(std::string(str_view));
    }

    this->traits_.set_supported_preset_modes(preset_modes);
  }
  // From Fan
  fan::FanTraits get_traits() override { return this->traits_; }

  // Print method for debugging
  void dump_config() override;
};

}  // namespace sec_touch
}  // namespace esphome
