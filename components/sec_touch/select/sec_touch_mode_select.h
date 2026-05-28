#pragma once

#include "esphome/core/component.h"
#include "esphome/components/select/select.h"
#include "../sec_touch.h"

namespace esphome {
namespace sec_touch {

class SecTouchFan;

class SecTouchModeSelect : public Component, public select::Select {
  static constexpr const char *TAG = "SecTouchModeSelect";

 public:
  SecTouchModeSelect(SECTouchComponent *parent, int level_id);

  void setup() override;
  void dump_config() override;
  // LATE so the Fan (also LATE) and the parent component are registered first.
  // Setup priorities are tied here, so we register the fan callback defensively.
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  void control(const std::string &value) override;
  void sync_from_fan_();

  SECTouchComponent *parent_;
  int level_id_;
  SecTouchFan *fan_{nullptr};
};

}  // namespace sec_touch
}  // namespace esphome
