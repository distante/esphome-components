#pragma once

#include <queue>
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#include "_definitions.h"

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
  /**
   * Use this for things that need to be updated on each loop
   */
  void register_recursive_update_listener(int property_id, UpdateCallbackListener listener);

  void update_now(bool fill_get_queue);
  void fill_get_queue_with_fans();

 protected:
  IncomingMessage incoming_message;
  std::queue<SetDataTask> data_set_queue;
  std::queue<std::unique_ptr<GetDataTask>> data_get_queue;
  std::map<int, UpdateCallbackListener> recursive_update_listeners;
  std::vector<int> recursive_update_ids;
  void notify_recursive_update_listeners(int property_id, int new_value);

  bool process_set_queue();
  bool process_get_queue();

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
