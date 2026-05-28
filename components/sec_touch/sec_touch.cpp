#include "sec_touch.h"
#include "XModemCRC.h"
#include "esphome/core/log.h"
#include "_definitions.h"
#include "fan/sec_touch_fan.h"

namespace esphome {
namespace sec_touch {

static const char *const TAG = "sec-touch";
static const char *const TAG_UART = "sec-touch-uart";

static const unsigned long TASK_TIMEOUT_MS = 2000;  // 2 seconds

SECTouchComponent::SECTouchComponent() {}

void SECTouchComponent::setup() {
  ESP_LOGI(TAG, "SEC-Touch setup initializing.");

  this->incoming_message.reset();
  this->add_manual_tasks_to_queue();
  this->add_recursive_tasks_to_get_queue();

  ESP_LOGI(TAG, " SEC-Touch setup complete.");
}

void SECTouchComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "SEC-Touch:");
  ESP_LOGCONFIG(TAG, "  total_register_fans: %d", this->recursive_update_ids.size());

  // Iterate through the map and log each property_id
  for (const auto &pair : this->recursive_update_listeners) {
    ESP_LOGCONFIG(TAG, "  - Recursive Updated Property ID: %d", pair.first);  // Log the property_id
  }
  for (const auto &pair : this->manual_update_listeners) {
    ESP_LOGCONFIG(TAG, "  - Manual Update Property ID: %d", pair.first);  // Log the property_id
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
        char hex_buf[128];
        int hex_pos = 0;
        int plen = this->incoming_message.buffer_index + 1;
        for (int i = 0; i < plen && hex_pos < (int) sizeof(hex_buf) - 4; i++) {
          hex_pos += snprintf(hex_buf + hex_pos, sizeof(hex_buf) - hex_pos, "%02X ",
                              (uint8_t) this->incoming_message.buffer[i]);
        }
        ESP_LOGW(TAG, "[watchdog] Task of type %s for property_id %d timed out — partial buffer (%d bytes: %s)",
                 EnumToString::TaskType(this->current_running_task_type), this->current_running_task_property_id_, plen,
                 hex_buf);
      } else {
        ESP_LOGW(TAG, "[watchdog] Task of type %s for property_id %d timed out — no response received",
                 EnumToString::TaskType(this->current_running_task_type), this->current_running_task_property_id_);
      }
      this->cleanup_after_task_complete(true, true);
    }

    if (this->data_task_queue.empty()) {
      if (!this->queue_was_idle_) {
        this->queue_was_idle_ = true;
        for (auto &cb : this->queue_empty_listeners) {
          cb();
        }
      }
      return;
    }

    if (millis() < this->task_ready_at_ms_) {
      return;
    }
    ESP_LOGD(TAG, "[loop] No Data available, processing task queue");
    this->process_task_queue();
    return;
  }

  ESP_LOGD(TAG, "[loop] Data available (Current buffer size: %d, Task queue size: %d)",
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

esphome::optional<SecTouchFan *> SECTouchComponent::get_fan(int level_id) {
  auto it = this->fans_.find(level_id);
  if (it != this->fans_.end()) {
    return it->second;
  }
  return esphome::optional<SecTouchFan *>{};
}

void SECTouchComponent::notify_update_listeners(int command_id, int property_id, int new_value) {
  ESP_LOGV(TAG, "notify_update_listeners command_id=%d property_id=%d", command_id, property_id);

  if (this->scan_mode_active_) {
    for (auto &cb : this->raw_message_listeners) {
      cb(command_id, property_id, new_value);
    }
    return;
  }

  auto recursive_listener = this->recursive_update_listeners.find(property_id);
  auto manual_listener = this->manual_update_listeners.find(property_id);

  if (recursive_listener != this->recursive_update_listeners.end()) {
    ESP_LOGV(TAG, "recursive_update_listener found for property_id %d", property_id);
    recursive_listener->second(property_id, new_value);
  }

  if (manual_listener != this->manual_update_listeners.end()) {
    ESP_LOGD(TAG, "manual_update_listener found for property_id %d", property_id);
    manual_listener->second(property_id, new_value);
  }

  if (recursive_listener == this->recursive_update_listeners.end() &&
      manual_listener == this->manual_update_listeners.end()) {
    if (this->raw_message_listeners.empty()) {
      ESP_LOGW(TAG, "No listener for property_id %d (value %d)", property_id, new_value);
      return;
    }
    for (auto &cb : this->raw_message_listeners) {
      cb(command_id, property_id, new_value);
    }
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
  ESP_LOGD(TAG, "Processing one task of %d -  type: %s", data_task_queue.size(),
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
  data_task_queue.pop_front();
}

int SECTouchComponent::store_data_to_incoming_message(uint8_t data) {
  ESP_LOGD(TAG_UART, "store_data_to_incoming_message %d", data);
  return this->incoming_message.store_data(data);
}

void SECTouchComponent::add_recursive_tasks_to_get_queue() {
  if (this->recursive_update_ids.empty()) {
    ESP_LOGW(TAG, "No property ids are registered for recursive tasks");
    return;
  }

  for (size_t i = 0; i < this->recursive_update_ids.size(); i++) {
    int id = this->recursive_update_ids[i];
    if (id == 0) {
      return;
    }
    // For now, just level is recursive
    this->data_task_queue.push_back(GetDataTask::create(TaskTargetType::LEVEL, id));
  }
}

void SECTouchComponent::add_manual_tasks_to_queue() {
  ESP_LOGD(TAG, "add_manual_tasks_to_queue");

  if (this->manual_update_ids[0] == 0) {
    ESP_LOGE(TAG, "No property ids are registered for manual tasks");
    return;
  }

  for (size_t i = 0; i < this->manual_update_ids.size(); i++) {
    int id = this->manual_update_ids[i];
    if (id == 0) {
      return;
    }

    // For now, just Label

    this->data_task_queue.push_back(GetDataTask::create(TaskTargetType::LABEL, id));
    // this->process_task_queue();  // Process the queue immediately
  }
}

void SECTouchComponent::register_recursive_update_listener(int property_id, UpdateCallbackListener listener) {
  ESP_LOGD(TAG, "register_recursive_update_listener for property_id %d", property_id);
  this->recursive_update_listeners[property_id] = std ::move(listener);
  this->recursive_update_ids.push_back(property_id);
}

void SECTouchComponent::register_manual_update_listener(int property_id, UpdateCallbackListener listener) {
  ESP_LOGD(TAG, "register_manual_update_listener for property_id %d", property_id);
  this->manual_update_listeners[property_id] = std::move(listener);
  this->manual_update_ids.push_back(property_id);
}

void SECTouchComponent::register_raw_message_listener(RawMessageCallbackListener listener) {
  this->raw_message_listeners.push_back(std::move(listener));
}

void SECTouchComponent::register_queue_empty_listener(QueueEmptyListener listener) {
  this->queue_empty_listeners.push_back(std::move(listener));
}

void SECTouchComponent::add_discovery_get_task(int property_id) {
  this->data_task_queue.push_back(GetDataTask::create_unchecked(property_id));
}

void SECTouchComponent::enter_scan_mode() {
  ESP_LOGI(TAG, "Entering scan mode — pausing regular polling");
  if (!this->data_task_queue.empty()) {
    ESP_LOGW(TAG, "Entering scan mode — discarding %d pending task(s)", this->data_task_queue.size());
  }
  this->data_task_queue.clear();
  this->incoming_message.reset();
  this->current_running_task_type = TaskType::NONE;
  this->task_start_time_ = 0;
  this->queue_was_idle_ = false;  // Force queue-empty callback to fire so scan tasks get queued
  this->scan_mode_active_ = true;
}

void SECTouchComponent::exit_scan_mode() {
  ESP_LOGI(TAG, "Exiting scan mode — resuming regular polling");
  this->scan_mode_active_ = false;
  this->add_recursive_tasks_to_get_queue();
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

  std::array<char, 64> message_buffer;

  int len = snprintf(message_buffer.data(), message_buffer.size(), "%c%d%c%d%c", STX, COMMANDID_GET, TAB,
                     task.property_id, TAB);
  unsigned short crc = GetXModemCRC(message_buffer.data(), len);
  len += snprintf(message_buffer.data() + len, message_buffer.size() - len, "%u%c", crc, ETX);

  ESP_LOGD(TAG_UART, "  buffer %s", message_buffer.data());
  this->write_array(reinterpret_cast<const uint8_t *>(message_buffer.data()), len);
}

void SECTouchComponent::send_set_message(SetDataTask &task) {
  std::array<char, 64> message_buffer;
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
      this->cleanup_after_task_complete();
    }
    return;
  }

  // NAK: device rejected the request (e.g. unknown property_id during scan)
  if (this->incoming_message.buffer[0] == STX && this->incoming_message.buffer[1] == NAK &&
      this->incoming_message.buffer[this->incoming_message.buffer_index] == ETX) {
    ESP_LOGW(TAG, "  Received NAK — property_id not supported by device");
    this->cleanup_after_task_complete(true);
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

  // Defensive: ensure message starts with STX and ends with ETX
  if (len < 7 || static_cast<uint8_t>(buf[0]) != STX || static_cast<uint8_t>(buf[len - 1]) != ETX) {
    char hex_buf[128];
    hex_buf[0] = '\0';
    int hex_pos = 0;
    for (int i = 0; i < len && hex_pos < (int) sizeof(hex_buf) - 4; i++) {
      hex_pos += snprintf(hex_buf + hex_pos, sizeof(hex_buf) - hex_pos, "%02X ", (uint8_t) buf[i]);
    }
    ESP_LOGE(TAG_UART, "  [process_data] Invalid message format (task=%s property_id=%d len=%d hex: %s). Task Failed",
             EnumToString::TaskType(this->current_running_task_type), this->current_running_task_property_id_, len,
             hex_buf);
    this->cleanup_after_task_complete(true);
    return;
  }

  // Find TABs
  int tab1 = -1, tab2 = -1, tab3 = -1;
  for (int i = 0, tabs = 0; i < len; ++i) {
    if (buf[i] == TAB) {
      if (tabs == 0)
        tab1 = i;
      else if (tabs == 1)
        tab2 = i;
      else if (tabs == 2)
        tab3 = i;
      ++tabs;
    }
  }
  if (tab1 == -1 || tab2 == -1 || tab3 == -1) {
    char hex_buf[128];
    hex_buf[0] = '\0';
    int hex_pos = 0;
    for (int i = 0; i < len && hex_pos < (int) sizeof(hex_buf) - 4; i++) {
      hex_pos += snprintf(hex_buf + hex_pos, sizeof(hex_buf) - hex_pos, "%02X ", (uint8_t) buf[i]);
    }
    ESP_LOGE(
        TAG_UART, "  [process_data] Not enough TABs in message (task=%s property_id=%d len=%d hex: %s). Task Failed",
        EnumToString::TaskType(this->current_running_task_type), this->current_running_task_property_id_, len, hex_buf);
    this->cleanup_after_task_complete(true);
    return;
  }

  // Extract command id, property id, value, crc
  int command_id = atoi(std::string(&buf[1], tab1 - 1).c_str());
  int property_id = atoi(std::string(&buf[tab1 + 1], tab2 - tab1 - 1).c_str());
  int value = atoi(std::string(&buf[tab2 + 1], tab3 - tab2 - 1).c_str());
  int crc = atoi(std::string(&buf[tab3 + 1], len - tab3 - 2).c_str());  // -2 to skip ETX

  ESP_LOGD(TAG, "  [process_data] command_id: %d, property_id: %d, value: %d, crc: %d", command_id, property_id, value,
           crc);

  // Optionally: validate CRC here if needed

  // Notify listeners
  this->notify_update_listeners(command_id, property_id, value);

  this->cleanup_after_task_complete();
  this->send_ack_message();  // Send ACK back
}

void SECTouchComponent::cleanup_after_task_complete(bool failed, bool is_timeout) {
  ESP_LOGD(TAG, "cleanup_after_task_complete called, failed: %s", failed ? "true" : "false");
  this->incoming_message.reset();
  this->current_running_task_type = TaskType::NONE;
  this->task_start_time_ = 0;
  this->last_scan_task_timed_out_ = is_timeout && this->scan_mode_active_;
  this->task_ready_at_ms_ = millis() + this->inter_task_delay_ms_;
}
}  // namespace sec_touch
}  // namespace esphome