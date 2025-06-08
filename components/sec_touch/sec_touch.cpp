#include "sec_touch.h"
#include "XModemCRC.h"
#include "esphome/core/log.h"
#include "_definitions.h"

namespace esphome {
namespace sec_touch {

static const char *const TAG = "sec-touch";
static const char *const TAG_UART = "sec-touch-uart";

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
  if (this->data_task_queue.empty() && !this->available()) {
    ESP_LOGD(TAG, "SEC-Touch update");
    this->add_recursive_tasks_to_get_queue();
  }
}

void SECTouchComponent::loop() {
  if (!this->available()) {
    ESP_LOGVV(TAG, "[loop] No Data available");
    if (this->data_task_queue.empty()) {
      return;
    }

    if (this->current_running_task_type != TaskType::NONE) {
      ESP_LOGD(TAG, "[loop] We are waiting for the response after a task of type %s",
               EnumToString::TaskType(this->current_running_task_type));
      return;  // We have a task running, so we don't need to process the queue
    }

    ESP_LOGD(TAG, "[loop] No Data available, processing task queue");
    this->process_task_queue();
    return;
  }

  ESP_LOGD(TAG, "[loop] Data available");
  // We have send some data and now we are waiting for the response
  uint8_t peakedData;
  this->peek_byte(&peakedData);

  // Noise or Heartbeat handling?
  if (this->incoming_message.buffer_index == -1 && peakedData != STX) {
    ESP_LOGD(TAG, "[loop]  Discarding noise byte (or maybe a Heartbeat with %d?)", peakedData);
    this->read_byte(&peakedData);
    return;
  }

  if (this->incoming_message.buffer_index != -1) {
    ESP_LOGE(TAG, "[loop]  The loop found a non-empty incoming message buffer, but we are not in the middle of "
                  "processing a message. This is unexpected.");
    return;
  }

  if (peakedData != STX) {
    ESP_LOGW(TAG, "  Discarding noise(?) byte %d, expected STX", peakedData);
    this->read_byte(&peakedData);  // Discard the noise
    return;
  }

  ESP_LOGD(TAG, "  Received STX %d. Starting to store Message", peakedData);

  // keep storing data in the incoming message until we reach ETX
  while (this->available()) {
    uint8_t data;
    this->read_byte(&data);
    this->store_data_to_incoming_message(data);
    // we need to exist if we reach ETX
    if (data == ETX) {
      ESP_LOGD(TAG, "  Received ETX %d, processing message", data);

      this->process_data_of_current_incoming_message();
      this->current_running_task_type = TaskType::NONE;  // Reset the current running task type
      return;
    }
  }
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

void SECTouchComponent::notify_update_listeners(int property_id, int new_value) {
  ESP_LOGV(TAG, "notify_update_listeners for property_id %d", property_id);

  auto recursive_listener = this->recursive_update_listeners.find(property_id);

  if (recursive_listener == this->recursive_update_listeners.end()) {
    ESP_LOGV(TAG, "No recursive_update_listeners found for property_id %d", property_id);
  } else {
    ESP_LOGV(TAG, "recursive_update_listener found for property_id %d", property_id);
    UpdateCallbackListener &listener = recursive_listener->second;
    listener(property_id, new_value);  // Call the listener
  }

  auto manual_listener = this->manual_update_listeners.find(property_id);

  if (manual_listener == this->manual_update_listeners.end()) {
    ESP_LOGV(TAG, "No manual_update_listeners found for property_id %d", property_id);
  } else {
    ESP_LOGD(TAG, "manual_update_listener found for property_id %d", property_id);
    UpdateCallbackListener &listener = manual_listener->second;
    listener(property_id, new_value);  // Call the listener
  }

  if (recursive_listener == this->recursive_update_listeners.end() &&
      manual_listener == this->manual_update_listeners.end()) {
    ESP_LOGE(TAG, "No listener found for property_id %d", property_id);
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

void SECTouchComponent::mark_current_get_queue_item_as_failed() {
  ESP_LOGE(TAG, "mark_current_get_queue_item_as_failed should not be called");
  // auto &task_ptr = this->data_get_queue.front();
  // auto &task = *task_ptr;
  // ESP_LOGD(TAG, "[FAILED Task] targetType \"%s\" and id \"%d\"", EnumToString::TaskTargetType(task.targetType),
  //          task.property_id);

  // this->incoming_message.reset();
  // this->data_get_queue.pop_front();

  // TODO: Retry?
}

void SECTouchComponent::add_recursive_tasks_to_get_queue() {
  if (this->recursive_update_ids.empty()) {
    ESP_LOGW(TAG, "No property ids are registered for recursive tasks");
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
  ESP_LOGD(TAG, "register_recursive_update_listener for property_id %d", property_id);
  this->manual_update_listeners[property_id] = std ::move(listener);
  this->manual_update_ids.push_back(property_id);
}

void SECTouchComponent::add_set_task(std::unique_ptr<SetDataTask> task) {
  ESP_LOGD(TAG, "add_set_task");
  this->data_task_queue.push_back(std::move(task));
  // this->process_task_queue();  // Process the queue immediately
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
    // this->send_ack_message(); // I do not think we need to send ACK back here, do we?
    this->incoming_message.reset();
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
    ESP_LOGE(TAG_UART, "  [process_data] Invalid message format. Task Failed");
    this->incoming_message.reset();
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
    ESP_LOGE(TAG_UART, "  [process_data] Not enough TABs in message. Task Failed");
    // this->mark_current_get_queue_item_as_failed();
    // TODO: SEND NACK??
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
  this->notify_update_listeners(property_id, value);

  this->incoming_message.reset();
  this->send_ack_message();  // Send ACK back
}

}  // namespace sec_touch
}  // namespace esphome