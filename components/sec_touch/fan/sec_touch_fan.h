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
  std::string get_mode_from_speed(int speed);
  void update_mode();
  fan::FanTraits traits_;

 public:
  SecTouchFan(SECTouchComponent *parent, int level_id, int label_id);

  void setup() override {
    this->traits_ = fan::FanTraits(false, true, false, 10);
    this->traits_.set_supported_preset_modes(FanModeEnum::getStringValues());
  }
  // From Fan
  fan::FanTraits get_traits() override { return this->traits_; }

  // Print method for debugging
  void printConfig();
};

}  // namespace sec_touch
}  // namespace esphome
