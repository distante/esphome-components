#pragma once

#include <queue>
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#include "_sec_touch_fan.h"

// #ifdef USE_BUTTON
// #include "esphome/components/button/button.h"
// #endif

namespace esphome {
namespace sec_touch {

class SECTouchComponent : public Component, public uart::UARTDevice {
  // #ifdef USE_BUTTON
  //   SUB_BUTTON(Poll)
  // #endif
 public:
  SECTouchComponent();
  void setup() override;
  void dump_config() override;
  void loop() override;
  void set_total_fan_pairs(int total_fan_pairs) { this->total_fan_pairs = total_fan_pairs; }

 public:
 protected:
  int total_fan_pairs;
  std::queue<SetDataTask> data_set_queue;
  std::queue<GetDataTask> data_get_queue;
  SecTouchFanManager fanManager;
};
}  // namespace sec_touch
}  // namespace esphome
