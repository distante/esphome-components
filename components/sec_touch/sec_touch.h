#pragma once

#include <queue>
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#include "_sec_touch_fan.h"

#include "esphome/components/button/button.h"
#include <typeinfo>
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
  void update_now(bool fill_get_queue);

 protected:
  int total_fan_pairs;
  IncomingMessage incoming_message;
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
  int store_data_to_incoming_message(uint8_t data);
  void reset_incoming_message();
  void mark_current_get_queue_item_as_failed();
  void send_ack_message();

  // QUEUE HANDLING
  void wait_for_ack_of_current_get_queue_item();
  void wait_for_data_of_current_get_queue_item();
  void process_data_for_current_get_queue_item();
};
}  // namespace sec_touch
}  // namespace esphome
