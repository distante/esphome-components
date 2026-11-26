#pragma once

#include <array>
#include <map>
#include <queue>
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "_definitions.h"
#include "data_message.h"
#include "message_router.h"
#include "scan_outcome.h"
#include "time_program_ids.h"

#include <typeinfo>
namespace esphome {
namespace sec_touch {

class SecTouchFan;
class TimeProgram;

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

  /**
   * Use this for global controller settings (not per-fan) that should be polled
   * at the configured `global_settings_update_interval` cadence — independent
   * from the fan-level recursive polling.
   */
  void register_global_setting_recursive_listener(int property_id, UpdateCallbackListener listener);

  /**
   * Same polling cadence as `register_global_setting_recursive_listener`, but the
   * listener gets the raw value string instead of its atoi result.
   */
  void register_global_setting_string_listener(int property_id, StringUpdateCallbackListener listener);

  /**
   * Sees every data message with its raw value, in every mode, before entity listeners.
   * Registered and unregistered property IDs alike.
   */
  void register_message_tap(MessageTap tap);
  void register_queue_empty_listener(QueueEmptyListener listener);

  /**
   * Hands the raw value of every time-program data message for `property_id` to `listener`. Puts the ID on no poll:
   * add_time_program_poll_id() does that. Several listeners may share one ID. The live state also uses it for the
   * pair's level ID, which the pair's fan polls.
   */
  void register_time_program_listener(int property_id, StringUpdateCallbackListener listener);
  /// Puts `property_id` on fan pair `pair`'s time-program poll, once. Returns false for an ID outside that pair's
  /// block.
  bool add_time_program_poll_id(int pair, int property_id);
  /// Sees every task that ends; see TaskEndListener.
  void register_task_end_listener(TaskEndListener listener);
  void add_discovery_get_task(int property_id);
  /// Queues a SET of any property ID, unknown ones included, for the sniffer's write probe. Only in scan mode:
  /// outside it nothing is queued, a warning is logged and the call returns false, so nothing else can write an
  /// unknown ID.
  bool add_discovery_set_task(int property_id, const char *value);

  void enter_scan_mode();
  void exit_scan_mode();
  /// Outcome of the latest task that ended in scan mode.
  ScanOutcome get_last_scan_outcome() const { return this->scan_outcome_.last(); }
  bool is_property_registered(int property_id) const {
    return this->recursive_update_listeners.count(property_id) > 0 ||
           this->manual_update_listeners.count(property_id) > 0 || this->string_update_listeners.count(property_id) > 0;
  }

  void set_inter_task_delay(uint32_t ms) { inter_task_delay_ms_ = ms; }
  void set_global_settings_update_interval_ms(uint32_t ms) { global_settings_update_interval_ms_ = ms; }
  void set_time_programs_update_interval_ms(uint32_t ms) { time_programs_update_interval_ms_ = ms; }
  bool is_scan_mode_active() const { return this->scan_mode_active_; }
  void set_optional_crc(bool v) { optional_crc_ = v; }

  void add_set_task(std::unique_ptr<SetDataTask> task);
  /**
   * Queues a SET of `property_id`, one per ID: when a SET of that ID is already queued, its value is replaced in place
   * instead. A SET in flight isn't touched; the new one queues behind it. Returns the SET's ticket (never 0), new on
   * a replace too, which its task carries to the task-end listeners; or 0 for an ID `target` doesn't accept.
   */
  uint32_t add_set_task_latest_wins(TaskTargetType target, int property_id, const char *value);
  /// Queues a GET of `property_id` ahead of every other GET, after the queued SETs. Returns the GET's ticket (never 0),
  /// which its task carries to the task-end listeners, or 0 for an ID `target` doesn't accept. Virtual so a host test
  /// can refuse one.
  virtual uint32_t add_priority_get_task(TaskTargetType target, int property_id);
  void add_recursive_tasks_to_get_queue();
  void add_manual_tasks_to_queue();
  void add_global_setting_tasks_to_queue();
  /// One poll of every pair's time program; restarts its interval.
  void add_time_program_tasks_to_queue();
  /// One poll of `pair`'s time program, outside the schedule: the interval runs on.
  void add_time_program_tasks_to_queue(int pair);
  void register_text_sensor(int id, text_sensor::TextSensor *sensor);
  esphome::optional<text_sensor::TextSensor *> get_text_sensor(int id);
  void register_fan(int level_id, SecTouchFan *fan);
  /// Makes `program` fan pair `pair`'s time program, for get_time_program().
  void register_time_program(int pair, TimeProgram *program) {
    if (is_time_program_pair(pair)) {
      this->time_programs_[pair - 1] = program;
    }
  }
  /// Fan pair `pair`'s time program, or nullptr when `time_programs` doesn't list it.
  TimeProgram *get_time_program(int pair) const {
    return is_time_program_pair(pair) ? this->time_programs_[pair - 1] : nullptr;
  }
  esphome::optional<SecTouchFan *> get_fan(int level_id);
  bool has_pending_set_task_for(int property_id) const;

 protected:
  IncomingMessage incoming_message;
  std::deque<std::unique_ptr<BaseTask>> data_task_queue;
  /// The task sent last, kept until it ends so a dropped GET can go back to the queue.
  std::unique_ptr<BaseTask> in_flight_task_;

  // Several listeners may share one property ID; each poll list holds an ID once.
  std::multimap<int, UpdateCallbackListener> recursive_update_listeners;
  std::vector<int> fan_level_property_ids_;

  std::multimap<int, UpdateCallbackListener> manual_update_listeners;
  std::vector<int> fan_label_property_ids_;

  std::vector<int> global_setting_property_ids_;

  std::multimap<int, StringUpdateCallbackListener> string_update_listeners;

  /// Per fan pair, by pair - 1: the IDs its time-program poll queues. Fixed size, filled while entities are built.
  std::array<TimeProgramPollList, TIME_PROGRAM_PAIR_COUNT> time_program_polls_;
  std::vector<TaskEndListener> task_end_listeners_;

  MessageRouter message_router_;
  std::vector<QueueEmptyListener> queue_empty_listeners;
  bool scan_mode_active_{false};
  /// The last ticket handed out (next_ticket_()).
  uint32_t last_ticket_{0};
  bool queue_was_idle_{false};

  std::map<int, text_sensor::TextSensor *> text_sensors;
  std::map<int, SecTouchFan *> fans_;
  /// Per fan pair, by pair - 1: its time program, or nullptr when it isn't listed.
  std::array<TimeProgram *, TIME_PROGRAM_PAIR_COUNT> time_programs_{};

  void notify_update_listeners(int command_id, int property_id, int new_value, const std::string &raw_value);
  TaskType current_running_task_type = TaskType::NONE;

  void process_task_queue();

  void send_get_message(GetDataTask &task);

  void send_set_message(SetDataTask &task);

  /**
   * Returns the index of the last byte stored in the buffer
   */
  int store_data_to_incoming_message(uint8_t data);
  /// The task in flight got its answer, or none: ends it with `end`.
  void cleanup_after_task_complete(TaskEnd end);
  /// Takes the task in flight off the line, records its outcome and starts the inter-task delay. Returns the task for
  /// the caller to end (end_task_) or send again.
  std::unique_ptr<BaseTask> finish_in_flight_task_(TaskEnd end);
  /// Takes the task in flight off the line and clears the watchdog, so the next task can go out.
  std::unique_ptr<BaseTask> take_in_flight_task_();
  /// The one way a task ends, dropped ones included: tells every task-end listener how, then frees it. Nothing for
  /// nullptr.
  void end_task_(std::unique_ptr<BaseTask> task, TaskEnd end);
  /// Ends `task` as dropped (BaseTask::dropped), as a timeout.
  void drop_task_(std::unique_ptr<BaseTask> task);
  /// A new task ticket, never 0.
  uint32_t next_ticket_();
  bool has_time_program_polls() const;
  void send_ack_message();
  void drop_data_message(DataMessageCheck check);

  uint32_t task_start_time_ = 0;
  uint32_t task_ready_at_ms_ = 0;
  uint32_t inter_task_delay_ms_ = 30;
  /// Codegen always sets both poll intervals; their defaults live in the YAML schema (`__init__.py`).
  uint32_t global_settings_update_interval_ms_ = 0;
  uint32_t last_global_settings_update_ms_ = 0;
  uint32_t time_programs_update_interval_ms_ = 0;
  uint32_t last_time_programs_update_ms_ = 0;
  int current_running_task_property_id_ = -1;
  ScanOutcomeTracker scan_outcome_;
  bool optional_crc_{false};
  void process_data_of_current_incoming_message();
  std::string extract_value_field(const char *buf, int len, const DataMessageTabs &tabs);
  void describe_missing_fields(const char *buf, const DataMessageTabs &tabs, char *out, size_t out_size);
  void describe_dropped_message(const char *buf, int len, const char *details, char *out, size_t out_size);
  void format_hex_dump(const uint8_t *buf, int len, char *out, size_t out_size);
};
}  // namespace sec_touch
}  // namespace esphome
