#pragma once

#include <queue>
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#include "_sec_touch_fan.h"

#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif

namespace esphome {
namespace sec_touch {

class SECTouchComponent : public Component, public uart::UARTDevice {
#ifdef USE_BUTTON
  SUB_BUTTON(process_get_queue)
#endif
 public:
  SECTouchComponent();
  void setup() override;
  void dump_config() override;
  void loop() override;
  void set_total_fan_pairs(int total_fan_pairs) { this->total_fan_pairs = total_fan_pairs; }

 protected:
  int total_fan_pairs;
  uint8_t incoming_message_buffer[64];  // Persistent buffer for incoming message
  size_t incoming_message_index = 0;
  std::queue<SetDataTask> data_set_queue;
  std::queue<GetDataTask> data_get_queue;
  SecTouchFanManager fanManager;

  bool process_set_queue();
  bool process_get_queue();
  void fill_get_queue_with_fans();

  void send_get_message(GetDataTask task);
  /**
   * Returns the index of the last byte stored in the buffer
   */
  int store_to_incoming_buffer(uint8_t data);
  void reset_incoming_buffer();

  void send_ack_message();
};
}  // namespace sec_touch
}  // namespace esphome
