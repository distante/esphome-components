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

class SECTouchComponent : public PollingComponent, public uart::UARTDevice {
 public:
  SECTouchComponent();
  void setup() override;
  void dump_config() override;
  void loop() override;
  void update() override;
  /**
   * Use this for things that need to be updated on each loop
   */
  void register_recursive_update_listener(int property_id, UpdateCallbackListener listener);

  /**
   * Use this for things that need to be updated when manually requested
   * like labels
   */
  void register_manual_update_listener(int property_id, UpdateCallbackListener listener);

  void add_set_task(std::unique_ptr<SetDataTask> task);
  void fill_get_queue_with_fans();
  bool process_get_queue();

 protected:
  IncomingMessage incoming_message;
  std::deque<std::unique_ptr<GetDataTask>> data_get_queue;
  std::deque<std::unique_ptr<SetDataTask>> data_set_queue;

  std::map<int, UpdateCallbackListener> recursive_update_listeners;
  std::vector<int> recursive_update_ids;

  std::map<int, UpdateCallbackListener> manual_update_listeners;
  std::vector<int> manual_update_ids;

  void notify_recursive_update_listeners(int property_id, int new_value);
  bool processing_queue = false;
  TaskType current_running_task_type = TaskType::NONE;
  /**
   * @returns true if the queue was processed
   */
  void process_set_queue();

  void handle_uart_input_for_get_queue();

  void send_get_message(GetDataTask &task);

  void send_set_message(SetDataTask &task);

  /**
   * Returns the index of the last byte stored in the buffer
   */
  int store_data_to_incoming_message(uint8_t data);
  void mark_current_get_queue_item_as_failed();
  void send_ack_message();

  // QUEUE HANDLING

  void process_data_for_current_get_queue_item();
};
}  // namespace sec_touch
}  // namespace esphome
