#include "sec_touch.h"
#include <algorithm>
#include "XModemCRC.h"
#include "data_message.h"
#include "get_retry.h"
#include "time_units.h"
#include "esphome/core/log.h"
#include "_definitions.h"

namespace esphome {
namespace sec_touch {

namespace {

/// Appends `id` to `ids` unless it is there already, so a second listener for an ID adds no second GET.
void push_once(std::vector<int> &ids, int id) {
  if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
    ids.push_back(id);
  }
}

/// push_once() for the global-settings poll, which asks the panel date (ID 1) right before the panel clock (ID 2)
/// whatever the registration order: clock_drift pairs each clock reading with the date read just before it.
void push_global_setting_once(std::vector<int> &ids, int id) {
  push_once(ids, id);
  auto date = std::find(ids.begin(), ids.end(), DATE_PROPERTY_ID);
  if (date == ids.end() || std::find(ids.begin(), ids.end(), CLOCK_PROPERTY_ID) == ids.end()) {
    return;
  }
  ids.erase(date);
  ids.insert(std::find(ids.begin(), ids.end(), CLOCK_PROPERTY_ID), DATE_PROPERTY_ID);
}

}  // namespace

static const char *const TAG = "sec_touch";
static const char *const TAG_UART = "sec_touch.uart";

SECTouchComponent::SECTouchComponent() {}

void SECTouchComponent::setup() {
  ESP_LOGI(TAG, "SEC-Touch setup initializing.");

  this->incoming_message.reset();
  this->add_manual_tasks_to_queue();
  this->add_recursive_tasks_to_get_queue();
  this->add_global_setting_tasks_to_queue();
  this->add_time_program_tasks_to_queue();

  ESP_LOGI(TAG, " SEC-Touch setup complete.");
}

void SECTouchComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "SEC-Touch:");
  ESP_LOGCONFIG(TAG, "  total_register_fans: %zu", this->fan_level_property_ids_.size());

  // Iterate through the map and log each property_id
  for (const auto &pair : this->recursive_update_listeners) {
    ESP_LOGCONFIG(TAG, "  - Recursive Updated Property ID: %d", pair.first);  // Log the property_id
  }
  for (const auto &pair : this->manual_update_listeners) {
    ESP_LOGCONFIG(TAG, "  - Manual Update Property ID: %d", pair.first);  // Log the property_id
  }
  ESP_LOGCONFIG(TAG, "  global_settings_update_interval_ms: %" PRIu32, this->global_settings_update_interval_ms_);
  for (int id : this->global_setting_property_ids_) {
    ESP_LOGCONFIG(TAG, "  - Global Setting Property ID: %d", id);
  }
  ESP_LOGCONFIG(TAG, "  optional_crc: %s", this->optional_crc_ ? "true" : "false");
  if (this->has_time_program_polls()) {
    ESP_LOGCONFIG(TAG, "  time_programs_update_interval_ms: %" PRIu32, this->time_programs_update_interval_ms_);
  }
  for (size_t i = 0; i < this->time_program_polls_.size(); i++) {
    for (int id : this->time_program_polls_[i]) {
      ESP_LOGCONFIG(TAG, "  - Time Program Property ID: %d (fan pair %zu)", id, i + 1);
    }
  }

  if (this->is_failed()) {
    ESP_LOGE(TAG, "  !!!! SETUP of SEC-Touch failed !!!!");
  }
}

// poll component update
void SECTouchComponent::update() {
  if (this->scan_mode_active_) {
    return;
  }
  if (this->data_task_queue.empty() && !this->available()) {
    ESP_LOGD(TAG, "SEC-Touch update");
    this->add_recursive_tasks_to_get_queue();
  }
}

void SECTouchComponent::loop() {
  if (!this->available()) {
    ESP_LOGVV(TAG, "[loop] No Data available");

    // Watchdog check: must run even when queue is empty because tasks are popped on dispatch.
    if (this->current_running_task_type != TaskType::NONE) {
      if (this->task_start_time_ == 0 || millis() - this->task_start_time_ <= TASK_TIMEOUT_MS) {
        ESP_LOGD(TAG, "[loop] We are waiting for the response after a task of type %s",
                 EnumToString::TaskType(this->current_running_task_type));
        return;
      }
      if (this->incoming_message.buffer_index >= 0) {
        int plen = this->incoming_message.buffer_index + 1;
        char hex_buf[128];
        this->format_hex_dump(reinterpret_cast<const uint8_t *>(this->incoming_message.buffer), plen, hex_buf,
                              sizeof(hex_buf));
        ESP_LOGW(TAG, "[watchdog] Task of type %s for property_id %d timed out — partial buffer (%d bytes: %s)",
                 EnumToString::TaskType(this->current_running_task_type), this->current_running_task_property_id_, plen,
                 hex_buf);
      } else {
        ESP_LOGW(TAG, "[watchdog] Task of type %s for property_id %d timed out — no response received",
                 EnumToString::TaskType(this->current_running_task_type), this->current_running_task_property_id_);
      }
      this->cleanup_after_task_complete(TaskEnd::TASK_END_TIMEOUT);
    }

    if (this->data_task_queue.empty()) {
      if (!this->scan_mode_active_ && !this->available() &&
          millis() - this->last_global_settings_update_ms_ >= this->global_settings_update_interval_ms_) {
        this->add_global_setting_tasks_to_queue();
      }
      if (!this->scan_mode_active_ && this->has_time_program_polls() &&
          millis() - this->last_time_programs_update_ms_ >= this->time_programs_update_interval_ms_) {
        this->add_time_program_tasks_to_queue();
      }
      if (!this->queue_was_idle_) {
        this->queue_was_idle_ = true;
        for (auto &cb : this->queue_empty_listeners) {
          cb();
        }
      }
      return;
    }

    if (!millis_reached(millis(), this->task_ready_at_ms_)) {
      return;
    }
    ESP_LOGD(TAG, "[loop] No Data available, processing task queue");
    this->process_task_queue();
    return;
  }

  ESP_LOGD(TAG, "[loop] Data available (Current buffer size: %d, Task queue size: %zu)",
           this->incoming_message.buffer_index + 1, this->data_task_queue.size());

  uint8_t peakedData;
  this->peek_byte(&peakedData);

  // Discard noise only when not already mid-message
  if (this->incoming_message.buffer_index == -1 && peakedData != STX) {
    ESP_LOGD(TAG, "[loop]  Discarding noise byte (or maybe a Heartbeat with %d?)", peakedData);
    this->read_byte(&peakedData);
    return;
  }

  // Read bytes until ETX or UART buffer empties. Handles both starting a new
  // message and continuing a partial message that spanned multiple loop() calls.
  bool got_etx = false;
  while (this->available()) {
    uint8_t data;
    this->read_byte(&data);
    this->store_data_to_incoming_message(data);
    if (data == ETX) {
      ESP_LOGD(TAG, "  Received ETX, processing message");
      got_etx = true;
      break;
    }
  }

  if (!got_etx) {
    ESP_LOGD(TAG, "  Partial message in buffer (%d bytes), waiting for more", this->incoming_message.buffer_index + 1);
    return;
  }

  this->process_data_of_current_incoming_message();
}

void SECTouchComponent::register_text_sensor(int id, text_sensor::TextSensor *sensor) {
  this->text_sensors[id] = sensor;
}

esphome::optional<text_sensor::TextSensor *> SECTouchComponent::get_text_sensor(int id) {
  auto it = this->text_sensors.find(id);
  if (it != this->text_sensors.end()) {
    return it->second;
  }
  return esphome::optional<text_sensor::TextSensor *>{};
}

void SECTouchComponent::register_fan(int level_id, SecTouchFan *fan) { this->fans_[level_id] = fan; }

bool SECTouchComponent::has_pending_set_task_for(int property_id) const {
  return this->has_queued_task_(TaskType::SET_DATA, property_id);
}

bool SECTouchComponent::has_queued_task_(TaskType type, int property_id) const {
  for (const auto &task : this->data_task_queue) {
    if (task->get_task_type() == type && task->property_id == property_id) {
      return true;
    }
  }
  return false;
}

esphome::optional<SecTouchFan *> SECTouchComponent::get_fan(int level_id) {
  auto it = this->fans_.find(level_id);
  if (it != this->fans_.end()) {
    return it->second;
  }
  return esphome::optional<SecTouchFan *>{};
}

void SECTouchComponent::notify_update_listeners(int command_id, int property_id, int new_value,
                                                const std::string &raw_value) {
  ESP_LOGV(TAG, "notify_update_listeners command_id=%d property_id=%d", command_id, property_id);

  Route route = this->message_router_.dispatch(command_id, property_id, raw_value, this->scan_mode_active_,
                                               this->is_property_registered(property_id));
  if (route == Route::WARN_NO_LISTENER) {
    ESP_LOGW(TAG, "No listener for property_id %d (raw '%s')", property_id, raw_value.c_str());
    return;
  }
  if (route != Route::DELIVER) {
    return;
  }

  auto strings = this->string_update_listeners.equal_range(property_id);
  for (auto it = strings.first; it != strings.second; ++it) {
    it->second(property_id, raw_value);
  }

  auto recursives = this->recursive_update_listeners.equal_range(property_id);
  for (auto it = recursives.first; it != recursives.second; ++it) {
    ESP_LOGV(TAG, "recursive_update_listener found for property_id %d", property_id);
    it->second(property_id, new_value);
  }

  auto manuals = this->manual_update_listeners.equal_range(property_id);
  for (auto it = manuals.first; it != manuals.second; ++it) {
    ESP_LOGD(TAG, "manual_update_listener found for property_id %d", property_id);
    it->second(property_id, new_value);
  }
}

// Unified task processing function
void SECTouchComponent::process_task_queue() {
  if (data_task_queue.empty()) {
    ESP_LOGD(TAG, "The queue is empty, nothing to process");
    this->current_running_task_type = TaskType::NONE;
    return;
  }
  auto &task_ptr = data_task_queue.front();
  BaseTask *task = task_ptr.get();
  ESP_LOGD(TAG, "Processing one task of %zu -  type: %s", data_task_queue.size(),
           EnumToString::TaskType(task->get_task_type()));

  this->current_running_task_type = task->get_task_type();
  this->task_start_time_ = millis();
  this->current_running_task_property_id_ = task->property_id;
  this->queue_was_idle_ = false;

  switch (task->get_task_type()) {
    case TaskType::SET_DATA:
      send_set_message(*static_cast<SetDataTask *>(task));
      break;
    case TaskType::GET_DATA:
      send_get_message(*static_cast<GetDataTask *>(task));
      break;
    default:
      ESP_LOGW(TAG, "Unknown task type in unified queue");
      break;
  }
  this->in_flight_task_ = std::move(task_ptr);
  data_task_queue.pop_front();
}

int SECTouchComponent::store_data_to_incoming_message(uint8_t data) {
  ESP_LOGD(TAG_UART, "store_data_to_incoming_message %d", data);
  return this->incoming_message.store_data(data);
}

void SECTouchComponent::add_recursive_tasks_to_get_queue() {
  if (this->fan_level_property_ids_.empty()) {
    ESP_LOGW(TAG, "No property ids are registered for recursive tasks");
    return;
  }

  for (size_t i = 0; i < this->fan_level_property_ids_.size(); i++) {
    int id = this->fan_level_property_ids_[i];
    if (id == 0) {
      return;
    }
    // For now, just level is recursive
    this->data_task_queue.push_back(GetDataTask::create(TaskTargetType::LEVEL, id));
  }
}

void SECTouchComponent::add_manual_tasks_to_queue() {
  ESP_LOGD(TAG, "add_manual_tasks_to_queue");

  if (this->fan_label_property_ids_.empty() || this->fan_label_property_ids_[0] == 0) {
    ESP_LOGE(TAG, "No property ids are registered for manual tasks");
    return;
  }

  for (size_t i = 0; i < this->fan_label_property_ids_.size(); i++) {
    int id = this->fan_label_property_ids_[i];
    if (id == 0) {
      return;
    }

    // For now, just Label

    this->data_task_queue.push_back(GetDataTask::create(TaskTargetType::LABEL, id));
    // this->process_task_queue();  // Process the queue immediately
  }
}

void SECTouchComponent::add_global_setting_tasks_to_queue() {
  this->last_global_settings_update_ms_ = millis();

  if (this->global_setting_property_ids_.empty()) {
    return;
  }

  for (int id : this->global_setting_property_ids_) {
    this->data_task_queue.push_back(GetDataTask::create(TaskTargetType::GLOBAL_SETTING, id));
  }
}

void SECTouchComponent::register_recursive_update_listener(int property_id, UpdateCallbackListener listener) {
  ESP_LOGD(TAG, "register_recursive_update_listener for property_id %d", property_id);
  this->recursive_update_listeners.emplace(property_id, std::move(listener));
  push_once(this->fan_level_property_ids_, property_id);
}

void SECTouchComponent::register_manual_update_listener(int property_id, UpdateCallbackListener listener) {
  ESP_LOGD(TAG, "register_manual_update_listener for property_id %d", property_id);
  this->manual_update_listeners.emplace(property_id, std::move(listener));
  push_once(this->fan_label_property_ids_, property_id);
}

void SECTouchComponent::register_global_setting_recursive_listener(int property_id, UpdateCallbackListener listener) {
  ESP_LOGD(TAG, "register_global_setting_recursive_listener for property_id %d", property_id);
  this->recursive_update_listeners.emplace(property_id, std::move(listener));
  push_global_setting_once(this->global_setting_property_ids_, property_id);
}

void SECTouchComponent::register_global_setting_string_listener(int property_id,
                                                                StringUpdateCallbackListener listener) {
  ESP_LOGD(TAG, "register_global_setting_string_listener for property_id %d", property_id);
  this->string_update_listeners.emplace(property_id, std::move(listener));
  push_global_setting_once(this->global_setting_property_ids_, property_id);
}

void SECTouchComponent::register_unpolled_string_listener(int property_id, StringUpdateCallbackListener listener) {
  ESP_LOGD(TAG, "register_unpolled_string_listener for property_id %d", property_id);
  this->string_update_listeners.emplace(property_id, std::move(listener));
}

bool SECTouchComponent::add_time_program_poll_id(int pair, int property_id) {
  auto slot = time_program_slot(property_id);
  if (!slot.has_value() || slot->pair != pair) {
    ESP_LOGE(TAG, "Property ID %d is not part of fan pair %d's time program", property_id, pair);
    return false;
  }
  return this->time_program_polls_[pair - 1].add(property_id);
}

void SECTouchComponent::register_task_end_listener(TaskEndListener listener) {
  this->task_end_listeners_.push_back(std::move(listener));
}

bool SECTouchComponent::has_time_program_polls() const {
  for (const auto &poll : this->time_program_polls_) {
    if (poll.size() > 0) {
      return true;
    }
  }
  return false;
}

void SECTouchComponent::add_time_program_tasks_to_queue() {
  this->last_time_programs_update_ms_ = millis();
  for (int pair = 1; pair <= TIME_PROGRAM_PAIR_COUNT; pair++) {
    this->add_time_program_tasks_to_queue(pair);
  }
}

void SECTouchComponent::add_time_program_tasks_to_queue(int pair) {
  if (!is_time_program_pair(pair)) {
    ESP_LOGE(TAG, "Fan pair %d has no time program", pair);
    return;
  }
  // The sniffer takes every answer during a scan; the poll after the scan reads the pair.
  if (this->scan_mode_active_) {
    ESP_LOGD(TAG, "Scan mode active, fan pair %d's time program is read after the scan", pair);
    return;
  }
  for (int id : this->time_program_polls_[pair - 1]) {
    // Repeated refreshes add no second GET of an ID.
    if (this->has_queued_task_(TaskType::GET_DATA, id)) {
      continue;
    }
    auto task = GetDataTask::create(TaskTargetType::TIME_PROGRAM, id);
    if (task == nullptr) {
      ESP_LOGE(TAG, "No GET of property_id %d as a time program", id);
      continue;
    }
    this->data_task_queue.push_back(std::move(task));
  }
}

void SECTouchComponent::register_message_tap(MessageTap tap) { this->message_router_.add_tap(std::move(tap)); }

void SECTouchComponent::register_queue_empty_listener(QueueEmptyListener listener) {
  this->queue_empty_listeners.push_back(std::move(listener));
}

void SECTouchComponent::add_discovery_get_task(int property_id) {
  this->data_task_queue.push_back(GetDataTask::create_unchecked(property_id));
}

bool SECTouchComponent::add_discovery_set_task(int property_id, const char *value) {
  if (!this->scan_mode_active_) {
    ESP_LOGW(TAG, "Unchecked SET of property_id %d refused: only allowed in scan mode", property_id);
    return false;
  }
  this->data_task_queue.push_back(SetDataTask::create_unchecked(property_id, value));
  return true;
}

void SECTouchComponent::enter_scan_mode() {
  ESP_LOGI(TAG, "Entering scan mode — pausing regular polling");
  if (!this->data_task_queue.empty()) {
    ESP_LOGW(TAG, "Entering scan mode — discarding %zu pending task(s)", this->data_task_queue.size());
  }
  std::deque<std::unique_ptr<BaseTask>> discarded;
  discarded.swap(this->data_task_queue);
  // Force the queue-empty callback to fire, once the line is free, so scan tasks get queued.
  this->queue_was_idle_ = false;
  this->scan_outcome_.begin_scan();
  this->scan_mode_active_ = true;

  // The task still in flight is dropped: its listeners hear it end now. The line stays busy until its reply or the
  // watchdog, so a late reply ends no scan task.
  this->drop_task_(std::move(this->in_flight_task_));
  for (auto &task : discarded) {
    this->drop_task_(std::move(task));
  }
}

void SECTouchComponent::exit_scan_mode() {
  ESP_LOGI(TAG, "Exiting scan mode — resuming regular polling");
  // The task still in flight is dropped: its listeners hear it end now. The line stays busy until its reply or the
  // watchdog, so a late reply ends no other task.
  this->drop_task_(std::move(this->in_flight_task_));
  this->scan_mode_active_ = false;
  this->add_recursive_tasks_to_get_queue();
  this->add_global_setting_tasks_to_queue();
  this->add_time_program_tasks_to_queue();
}

uint32_t SECTouchComponent::add_set_task_latest_wins(TaskTargetType target, int property_id, const char *value) {
  for (auto &task : this->data_task_queue) {
    if (task->get_task_type() == TaskType::SET_DATA && task->property_id == property_id) {
      ESP_LOGD(TAG, "Replacing the queued SET of property_id %d with '%s'", property_id, value);
      static_cast<SetDataTask *>(task.get())->set_value(value);
      task->ticket = this->next_ticket_();
      return task->ticket;
    }
  }
  auto task = SetDataTask::create(target, property_id, value);
  if (task == nullptr) {
    ESP_LOGE(TAG, "No SET of property_id %d as %s", property_id, EnumToString::TaskTargetType(target));
    return 0;
  }
  task->ticket = this->next_ticket_();
  const uint32_t ticket = task->ticket;
  this->add_set_task(std::move(task));
  return ticket;
}

uint32_t SECTouchComponent::add_priority_get_task(TaskTargetType target, int property_id) {
  auto task = GetDataTask::create(target, property_id);
  if (task == nullptr) {
    ESP_LOGE(TAG, "No GET of property_id %d as %s", property_id, EnumToString::TaskTargetType(target));
    return 0;
  }
  task->ticket = this->next_ticket_();
  const uint32_t ticket = task->ticket;
  auto it = this->data_task_queue.begin();
  while (it != this->data_task_queue.end() && (*it)->get_task_type() == TaskType::SET_DATA) {
    ++it;
  }
  this->data_task_queue.insert(it, std::move(task));
  return ticket;
}

uint32_t SECTouchComponent::next_ticket_() {
  this->last_ticket_++;
  if (this->last_ticket_ == 0) {
    this->last_ticket_ = 1;  // 0 means no task
  }
  return this->last_ticket_;
}

void SECTouchComponent::add_set_task(std::unique_ptr<SetDataTask> task) {
  auto it = this->data_task_queue.begin();
  while (it != this->data_task_queue.end() && (*it)->get_task_type() == TaskType::SET_DATA) {
    ++it;
  }
  ESP_LOGD(TAG, "add_set_task: inserting at priority position %d of %d", (int) (it - this->data_task_queue.begin()),
           (int) this->data_task_queue.size());

  this->data_task_queue.insert(it, std::move(task));

  // WARNING: Do not add get tasks to update here. For now, we will just wait for the usual update cycle
  // if you add a get task here a recursive loop will be created (TODO?)
}

void SECTouchComponent::send_get_message(GetDataTask &task) {
  ESP_LOGD(TAG_UART, "send_get_message");

  std::array<char, FRAME_BUFFER_SIZE> message_buffer;

  int len = snprintf(message_buffer.data(), message_buffer.size(), "%c%d%c%d%c", STX, COMMANDID_GET, TAB,
                     task.property_id, TAB);
  unsigned short crc = GetXModemCRC(message_buffer.data(), len);
  len += snprintf(message_buffer.data() + len, message_buffer.size() - len, "%u%c", crc, ETX);

  ESP_LOGD(TAG_UART, "  buffer %s", message_buffer.data());
  this->write_array(reinterpret_cast<const uint8_t *>(message_buffer.data()), len);
}

void SECTouchComponent::send_set_message(SetDataTask &task) {
  std::array<char, FRAME_BUFFER_SIZE> message_buffer;
  int len = snprintf(message_buffer.data(), message_buffer.size(), "%c%d%c%d%c%s%c", STX, COMMANDID_SET, TAB,
                     task.property_id, TAB, task.value, TAB);

  unsigned short crc = GetXModemCRC(message_buffer.data(), len);
  len += snprintf(message_buffer.data() + len, message_buffer.size() - len, "%u%c", crc, ETX);

  ESP_LOGD(TAG, "  buffer %s", message_buffer.data());
  this->write_array(reinterpret_cast<const uint8_t *>(message_buffer.data()), len);
}

void SECTouchComponent::send_ack_message() {
  uint8_t data[] = {STX, ACK, ETX};
  this->write_array(data, sizeof(data));
  ESP_LOGD(TAG, "SendMessageAck sended");
}

void SECTouchComponent::format_hex_dump(const uint8_t *buf, int len, char *out, size_t out_size) {
  if (out_size == 0) {
    return;
  }
  out[0] = '\0';
  int pos = 0;
  for (int i = 0; i < len && pos < (int) out_size - 4; i++) {
    pos += snprintf(out + pos, out_size - pos, "%02X ", buf[i]);
  }
}

void SECTouchComponent::describe_missing_fields(const char *buf, const DataMessageTabs &tabs, char *out,
                                                size_t out_size) {
  char parsed_buf[96];
  int parsed_pos = 0;
  parsed_buf[0] = '\0';
  if (tabs.tab1 != -1) {
    int cmd = atoi(std::string(&buf[1], tabs.tab1 - 1).c_str());
    parsed_pos += snprintf(parsed_buf + parsed_pos, sizeof(parsed_buf) - parsed_pos, "command_id=%d", cmd);
  }
  if (tabs.tab1 != -1 && tabs.tab2 != -1) {
    int prop = atoi(std::string(&buf[tabs.tab1 + 1], tabs.tab2 - tabs.tab1 - 1).c_str());
    parsed_pos += snprintf(parsed_buf + parsed_pos, sizeof(parsed_buf) - parsed_pos, " property_id=%d", prop);
  }

  // TABs are discovered left-to-right, so tabN == -1 implies tab(N+1)... == -1.
  const char *missing = (tabs.tab1 == -1)   ? "command_id, property_id, value, crc"
                        : (tabs.tab2 == -1) ? "property_id, value, crc"
                                            : "value, crc";

  snprintf(out, out_size, "parsed: [%s] missing: [%s] ", parsed_buf, missing);
}

void SECTouchComponent::describe_dropped_message(const char *buf, int len, const char *details, char *out,
                                                 size_t out_size) {
  char hex_buf[128];
  this->format_hex_dump(reinterpret_cast<const uint8_t *>(buf), len, hex_buf, sizeof(hex_buf));
  snprintf(out, out_size, "task=%s property_id=%d len=%d %shex: %s",
           EnumToString::TaskType(this->current_running_task_type), this->current_running_task_property_id_, len,
           details, hex_buf);
}

std::string SECTouchComponent::extract_value_field(const char *buf, int len, const DataMessageTabs &tabs) {
  if (tabs.tab3 != -1) {
    return std::string(&buf[tabs.tab2 + 1], tabs.tab3 - tabs.tab2 - 1);
  }
  return std::string(&buf[tabs.tab2 + 1], len - tabs.tab2 - 2);  // -2 to skip ETX
}

void SECTouchComponent::process_data_of_current_incoming_message() {
  ESP_LOGD(TAG, "  [process_data] buffer: %s", this->incoming_message.buffer);

  /*
Now, we need to extract the parts of the message. It can be either:

```
[STX][ACK][ETX]
```
*/

  // This is the ACK
  if (this->incoming_message.buffer[0] == STX && this->incoming_message.buffer[1] == ACK &&
      this->incoming_message.buffer[this->incoming_message.buffer_index] == ETX) {
    ESP_LOGD(TAG, "  Received ACK message");
    if (this->current_running_task_type == TaskType::GET_DATA) {
      // GET: ACK confirms the device received our request; the data response follows.
      // Keep task state so the queue-empty callback does not fire prematurely.
      this->incoming_message.reset();
    } else {
      // SET: ACK is the complete response — task is done.
      this->cleanup_after_task_complete(TaskEnd::TASK_END_ANSWERED);
    }
    return;
  }

  // NAK: device rejected the request (e.g. unknown property_id during scan)
  if (this->incoming_message.buffer[0] == STX && this->incoming_message.buffer[1] == NAK &&
      this->incoming_message.buffer[this->incoming_message.buffer_index] == ETX) {
    ESP_LOGW(TAG, "  Received NAK — property_id not supported by device");
    this->cleanup_after_task_complete(TaskEnd::TASK_END_NAK);
    return;
  }

  /*
  Now we have a full message like this:
  ```log
[STX]32[TAB]173[TAB]5[TAB]42625[ETX]
```

where:
- `32` is the command id (in this case a for a SET request).
- `173` is the property_id of the fan pair.
- `5` is the value assigned to that property_id.
- `42625` is the checksum (probably?).
*/

  // Parse the message: [STX]32[TAB]173[TAB]5[TAB]42625[ETX]
  // Find the positions of the delimiters
  const char *buf = this->incoming_message.buffer;
  int len = this->incoming_message.buffer_index + 1;

  // During a GET, a frame too short for data is most likely the panel's ACK with a flipped bit, and the data
  // frame is still on the way. Leave the GET open: that frame or the watchdog ends it. Not ACKed (issue #50).
  if (len < DATA_MESSAGE_MIN_LEN && this->current_running_task_type == TaskType::GET_DATA) {
    char context[384];
    this->describe_dropped_message(buf, len, "", context, sizeof(context));
    ESP_LOGW(TAG_UART, "  [process_data] Frame too short for data (%s). Waiting for the data frame", context);
    this->incoming_message.reset();
    return;
  }

  // Defensive: ensure message starts with STX and ends with ETX
  if (len < DATA_MESSAGE_MIN_LEN || static_cast<uint8_t>(buf[0]) != STX || static_cast<uint8_t>(buf[len - 1]) != ETX) {
    char context[384];
    this->describe_dropped_message(buf, len, "", context, sizeof(context));
    ESP_LOGE(TAG_UART, "  [process_data] Invalid message format (%s). Task Failed", context);
    this->cleanup_after_task_complete(TaskEnd::TASK_END_MALFORMED);
    return;
  }

  DataMessageTabs tabs = find_data_message_tabs(buf, len);
  DataMessageCheck check = check_data_message(buf, len, tabs, this->optional_crc_);
  if (check == DataMessageCheck::DATA_MISSING_FIELDS) {
    char details[160];
    this->describe_missing_fields(buf, tabs, details, sizeof(details));
    char context[384];
    this->describe_dropped_message(buf, len, details, context, sizeof(context));
    ESP_LOGE(TAG_UART, "  [process_data] %s (%s). Task Failed", data_message_check_reason(check), context);
    this->drop_data_message(check);
    return;
  }
  if (check == DataMessageCheck::DATA_CRC_MISMATCH || check == DataMessageCheck::DATA_CRC_NOT_A_NUMBER) {
    char context[384];
    this->describe_dropped_message(buf, len, "", context, sizeof(context));
    ESP_LOGW(TAG_UART, "  [process_data] %s (%s). Task Failed", data_message_check_reason(check), context);
    this->drop_data_message(check);
    return;
  }
  if (check == DataMessageCheck::DATA_ROUTE_UNCHECKED) {
    ESP_LOGD(TAG, "  [process_data] No CRC field in message (optional_crc=true), continuing");
  }

  // Extract command id, property id, value, crc
  int command_id = atoi(std::string(&buf[1], tabs.tab1 - 1).c_str());
  int property_id = atoi(std::string(&buf[tabs.tab1 + 1], tabs.tab2 - tabs.tab1 - 1).c_str());
  std::string raw_value = this->extract_value_field(buf, len, tabs);
  int value = atoi(raw_value.c_str());
  int crc = (tabs.tab3 != -1) ? atoi(std::string(&buf[tabs.tab3 + 1], len - tabs.tab3 - 2).c_str()) : -1;

  ESP_LOGD(TAG, "  [process_data] command_id: %d, property_id: %d, value: %d (raw '%s'), crc: %d", command_id,
           property_id, value, raw_value.c_str(), crc);

  // Notify listeners
  this->notify_update_listeners(command_id, property_id, value, raw_value);

  this->cleanup_after_task_complete(TaskEnd::TASK_END_ANSWERED);
  this->send_ack_message();  // Send ACK back
}

// A dropped data frame is still ACKed. Without the ACK the panel ignores the next request until the
// watchdog fires, and it never resends a frame, so the ACK gives nothing up.
// A GET whose reply this drops goes back to the front of the queue once, to be sent again after the ACK (issue #52).
void SECTouchComponent::drop_data_message(DataMessageCheck check) {
  std::unique_ptr<BaseTask> task = this->finish_in_flight_task_(TaskEnd::TASK_END_MALFORMED);
  this->send_ack_message();

  if (task != nullptr &&
      should_retry_get(task->get_task_type() == TaskType::GET_DATA, check, task->retried, this->scan_mode_active_)) {
    ESP_LOGI(TAG, "Sending the GET for property_id %d once more after dropping its reply", task->property_id);
    task->retried = true;
    this->data_task_queue.push_front(std::move(task));
    return;
  }
  this->end_task_(std::move(task), TaskEnd::TASK_END_MALFORMED);
}

void SECTouchComponent::cleanup_after_task_complete(TaskEnd end) {
  this->end_task_(this->finish_in_flight_task_(end), end);
}

std::unique_ptr<BaseTask> SECTouchComponent::finish_in_flight_task_(TaskEnd end) {
  ESP_LOGD(TAG, "Task ended: %s", task_end_name(end));
  std::unique_ptr<BaseTask> task = this->take_in_flight_task_();
  // A reply to a task already dropped (no task left) is no scan query's: it leaves the outcome alone too.
  this->scan_outcome_.record(end, this->scan_mode_active_,
                             task != nullptr && task->targetType == TaskTargetType::SNIFFER);
  this->task_ready_at_ms_ = millis() + this->inter_task_delay_ms_;
  return task;
}

std::unique_ptr<BaseTask> SECTouchComponent::take_in_flight_task_() {
  this->incoming_message.reset();
  this->current_running_task_type = TaskType::NONE;
  this->task_start_time_ = 0;
  return std::move(this->in_flight_task_);
}

void SECTouchComponent::drop_task_(std::unique_ptr<BaseTask> task) {
  if (task == nullptr) {
    return;
  }
  task->dropped = true;
  this->end_task_(std::move(task), TaskEnd::TASK_END_TIMEOUT);
}

void SECTouchComponent::end_task_(std::unique_ptr<BaseTask> task, TaskEnd end) {
  if (task == nullptr) {
    return;
  }
  for (const auto &listener : this->task_end_listeners_) {
    listener(*task, end);
  }
}
}  // namespace sec_touch
}  // namespace esphome
