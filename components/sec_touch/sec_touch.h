#pragma once

#include <map>
#include <queue>
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "_definitions.h"

#include "esphome/components/button/button.h"
#include <typeinfo>
namespace esphome {
namespace sec_touch {

class SecTouchFan;

class SECTouchComponent : public PollingComponent, public uart::UARTDevice {
 public:
  SECTouchComponent();
  void setup() override;
  void dump_config() override;
  void loop() override;
  void update() override;
  float get_setup_priority() const override {
    return setup_priority::LATE;  // Use LATE priority to ensure all components are set up first
  }
  /**
   * Use this for things that need to be updated on each loop
   */
  void register_recursive_update_listener(int property_id, UpdateCallbackListener listener);

  /**
   * Use this for things that need to be updated when manually requested
   * like labels.
   *
   * It is call one time on setup and then it is up to the user to call it
   */
  void register_manual_update_listener(int property_id, UpdateCallbackListener listener);

  void register_raw_message_listener(RawMessageCallbackListener listener);
  void register_queue_empty_listener(QueueEmptyListener listener);
  void add_discovery_get_task(int property_id);

  void enter_scan_mode();
  void exit_scan_mode();
  bool get_last_scan_task_timed_out() const { return last_scan_task_timed_out_; }
  bool is_property_registered(int property_id) const {
    return this->recursive_update_listeners.count(property_id) > 0 ||
           this->manual_update_listeners.count(property_id) > 0;
  }

  void set_inter_task_delay(uint32_t ms) { inter_task_delay_ms_ = ms; }

  void add_set_task(std::unique_ptr<SetDataTask> task);
  void add_recursive_tasks_to_get_queue();
  void add_manual_tasks_to_queue();
  void register_text_sensor(int id, text_sensor::TextSensor *sensor);
  esphome::optional<text_sensor::TextSensor *> get_text_sensor(int id);
  void register_fan(int level_id, SecTouchFan *fan);
  esphome::optional<SecTouchFan *> get_fan(int level_id);

 protected:
  IncomingMessage incoming_message;
  std::deque<std::unique_ptr<BaseTask>> data_task_queue;

  std::map<int, UpdateCallbackListener> recursive_update_listeners;
  std::vector<int> recursive_update_ids;

  std::map<int, UpdateCallbackListener> manual_update_listeners;
  std::vector<int> manual_update_ids;

  std::vector<RawMessageCallbackListener> raw_message_listeners;
  std::vector<QueueEmptyListener> queue_empty_listeners;
  bool scan_mode_active_{false};
  bool queue_was_idle_{false};

  std::map<int, text_sensor::TextSensor *> text_sensors;
  std::map<int, SecTouchFan *> fans_;

  void notify_update_listeners(int command_id, int property_id, int new_value);
  TaskType current_running_task_type = TaskType::NONE;

  void process_task_queue();

  void send_get_message(GetDataTask &task);

  void send_set_message(SetDataTask &task);

  /**
   * Returns the index of the last byte stored in the buffer
   */
  int store_data_to_incoming_message(uint8_t data);
  void cleanup_after_task_complete(bool failed = false, bool is_timeout = false);
  void send_ack_message();

  unsigned long task_start_time_ = 0;
  unsigned long task_ready_at_ms_ = 0;
  uint32_t inter_task_delay_ms_ = 30;
  int current_running_task_property_id_ = -1;
  bool last_scan_task_timed_out_ = false;
  void process_data_of_current_incoming_message();
};
}  // namespace sec_touch
}  // namespace esphome
