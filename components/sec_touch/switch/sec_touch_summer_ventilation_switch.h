#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "../sec_touch.h"

namespace esphome {
namespace sec_touch {

class SecTouchSummerVentilationSwitch : public Component, public switch_::Switch {
  static const char *const TAG;

 public:
  SecTouchSummerVentilationSwitch(SECTouchComponent *parent);

  float get_setup_priority() const override { return setup_priority::LATE; }
  void dump_config() override;

 protected:
  void write_state(bool state) override;

  SECTouchComponent *parent_;
};

}  // namespace sec_touch
}  // namespace esphome
