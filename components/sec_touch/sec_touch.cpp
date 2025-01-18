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

  this->fill_get_queue_with_fans();
  this->incoming_message.reset();

  ESP_LOGI(TAG, " SEC-Touch setup complete.");
}

void SECTouchComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "SEC-Touch:");
  ESP_LOGCONFIG(TAG, "  total_register_fans: %d", this->recursive_update_ids.size());

  // Iterate through the map and log each property_id
  for (const auto &pair : this->recursive_update_listeners) {
    ESP_LOGCONFIG(TAG, "  - Fan Property ID: %d", pair.first);  // Log the property_id
  }

  if (this->is_failed()) {
    ESP_LOGE(TAG, "  !!!! SETUP of SEC-Touch failed !!!!");
  }
}

void SECTouchComponent::update() {
  if (this->data_set_queue.empty() && !this->available()) {
    ESP_LOGD(TAG, "SEC-Touch update");
    this->fill_get_queue_with_fans();
    this->process_get_queue();
  }
}

void SECTouchComponent::loop() {
  if (!this->available()) {  // We are not waiting any response

    // If no other task is running, lets run all automatic set tasks
    if (this->current_running_task_type == TaskType::NONE && this->data_set_queue.size() > 0) {
      this->process_set_queue();
    }
    return;
  }

  ESP_LOGD(TAG, "SEC-Touch loop Data available");
  // We have send some data and now we are waiting for the response
  uint8_t peakedData;
  this->peek_byte(&peakedData);
  if (peakedData == NOISE && this->incoming_message.buffer_index == -1) {
    ESP_LOGD(TAG, "  Discarding noise byte");
    this->read_byte(&peakedData);  // Discard the noise
    return;
  }

  if (this->current_running_task_type == TaskType::AUTO_GET) {
    this->handle_uart_input_for_get_queue();
    return;
  }

  if (this->current_running_task_type == TaskType::MANUAL_SET) {
    while (this->available()) {
      uint8_t data;
      this->read_byte(&data);
      this->store_data_to_incoming_message(data);
    }

    ESP_LOGD(TAG_UART, "  Received MANUAL_SET Response %s", this->incoming_message.buffer);
    if (this->incoming_message.buffer_index == 2 && this->incoming_message.buffer[0] == STX &&
        this->incoming_message.buffer[1] == ACK && this->incoming_message.buffer[2] == ETX) {
      if (!this->data_set_queue.empty()) {
        ESP_LOGI(TAG_UART, "  MANUAL_SET Successful for Task targetType \"%s\" and property_id \"%d\"",
                 EnumToString::TaskTargetType(this->data_set_queue.front()->targetType),
                 this->data_set_queue.front()->property_id);
      }

    } else {
      ESP_LOGE(TAG_UART, "  MANUAL_SET was not successful");
    }

    // Create create a priority get update after set.

    if (!this->data_set_queue.empty()) {
      auto task = GetDataTask::create(TaskTargetType::LEVEL, this->data_set_queue.front()->property_id);
      this->data_get_queue.push_front(std::move(task));
      this->data_set_queue.pop_front();
    } else {
      ESP_LOGE(TAG, "  No task in the set queue to create a get task");
    }

    this->incoming_message.reset();

    if (this->data_set_queue.empty()) {
      this->current_running_task_type = TaskType::NONE;

      ESP_LOGD(TAG, "  No more tasks in the set queue, calling get tasks");
      this->process_get_queue();
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

void SECTouchComponent::notify_recursive_update_listeners(int property_id, int new_value) {
  ESP_LOGD(TAG, "notify_recursive_update_listeners for property_id %d", property_id);

  auto it = this->recursive_update_listeners.find(property_id);
  if (it == this->recursive_update_listeners.end()) {
    ESP_LOGE(TAG, "No listener found for property_id %d", property_id);
    return;
  }

  UpdateCallbackListener &listener = it->second;
  listener(property_id, new_value);  // Call the listener
}

void SECTouchComponent::handle_uart_input_for_get_queue() {
  ESP_LOGD(TAG, "[handle_uart_input_for_get_queue]--------");
  int current_index = -1;

  int total_tabs = 0;
  int total_etx = 0;
  int total_stx = 0;

  bool ack_already_failed = false;

  while (this->available()) {
    uint8_t data;
    this->read_byte(&data);

    ESP_LOGD(TAG_UART, "  Byte received: %d", data);

    bool start_of_ack_message = this->incoming_message.buffer_index == 0;

    if (start_of_ack_message && data != ACK) {
      if (ack_already_failed) {
        ESP_LOGW(TAG,
                 "Starting to store message but the data is not ACK in the second attempt. Discarding all data: %d",
                 data);
        this->incoming_message.reset();
        return;
      }

      ack_already_failed = true;
      ESP_LOGW(TAG, "Starting to store message but the data is not ACK. Discarding data: %d", data);
      continue;
    } else if (!start_of_ack_message && data == NOISE) {
      ESP_LOGE(TAG, "  received NOISE (255) inside message discarding all data: %d", data);
      this->incoming_message.reset();
      return;
    }

    bool inside_id = total_etx == 1 && total_stx > 1 && total_tabs == 1;
    bool inside_value = total_etx == 1 && total_stx > 1 && total_tabs == 2;

    current_index = this->store_data_to_incoming_message(data);

    if (inside_id) {
      ESP_LOGD(TAG_UART, "    saving into id");
      this->incoming_message.add_to_returned_id(data);
    } else if (inside_value) {
      ESP_LOGD(TAG_UART, "    saving into value");
      this->incoming_message.add_to_returned_value(data);
    } else {
      ESP_LOGD(TAG_UART, "    saving into buffer without id or value");
    }

    if (data == ETX) {
      total_etx++;
    } else if (data == TAB) {
      total_tabs++;
    } else if (data == STX) {
      total_stx++;
    }
  }

  if (this->incoming_message.buffer_index == 0) {
    ESP_LOGW(TAG, "  No valid data was found on the UART buffer");
  } else {
    this->incoming_message.buffer[current_index + 1] = '\0';  // Explicit null terminator

    ESP_LOGD(TAG_UART, "  Buffer %s", this->incoming_message.buffer);
    int returned_id = this->incoming_message.get_returned_id_as_int();
    int returned_value = this->incoming_message.get_returned_value_as_int();
    ESP_LOGD(TAG_UART, "  returned_id: %d, extracted_value: %d", returned_id, returned_value);
    ESP_LOGD(TAG_UART, "[handle_uart_input_for_get_queue]------------------------------------------");
    this->send_ack_message();
    this->process_data_for_current_get_queue_item();

    if (!this->data_get_queue.empty()) {
      ESP_LOGD(TAG, "[handle_uart_input_for_get_queue] Processing next get queue item");
      this->process_get_queue();
    } else {
      this->current_running_task_type = TaskType::NONE;
    }
  }
}

void SECTouchComponent::process_set_queue() {
  ESP_LOGD(TAG, "process_set_queue size %d", this->data_set_queue.size());

  if (this->data_set_queue.size() == 0) {
    ESP_LOGE(TAG, "process_set_queue No data in the given queue");
    return;
  }

  this->current_running_task_type = TaskType::MANUAL_SET;

  ESP_LOGD(TAG, "process_set_queue with size %d", this->data_set_queue.size());
  // Access the smart pointer from the queue
  auto &task_ptr = this->data_set_queue.front();

  // Dereference the smart pointer to access the actual object
  auto &task = *task_ptr;
  ESP_LOGD(TAG, "process_set_queue Task targetType \"%s\" and property_id \"%d\"",
           EnumToString::TaskTargetType(task.targetType), task.property_id);

  this->data_set_queue.front()->state = TaskState::TO_BE_PROCESSED;
  this->send_set_message(task);
  return;
}

bool SECTouchComponent::process_get_queue() {
  const std::string queueLoggingName = "data-get-queue";
  if (this->data_get_queue.size() == 0) {
    ESP_LOGE(TAG, "No data in the given queue %s", queueLoggingName.c_str());
    return false;
  }

  this->current_running_task_type = TaskType::AUTO_GET;
  ESP_LOGD(TAG, "process_get_queue \"%s\" with size %d", queueLoggingName.c_str(), this->data_get_queue.size());
  // Access the smart pointer from the queue
  auto &task_ptr = this->data_get_queue.front();

  // Dereference the smart pointer to access the actual object
  auto &task = *task_ptr;
  ESP_LOGD(TAG, "Task targetType \"%s\" and property_id \"%d\"", EnumToString::TaskTargetType(task.targetType),
           task.property_id);

  this->data_get_queue.front()->state = TaskState::TO_BE_PROCESSED;
  this->send_get_message(task);

  return true;
}

int SECTouchComponent::store_data_to_incoming_message(uint8_t data) { return this->incoming_message.store_data(data); }

void SECTouchComponent::mark_current_get_queue_item_as_failed() {
  auto &task_ptr = this->data_get_queue.front();
  auto &task = *task_ptr;
  ESP_LOGD(TAG, "[FAILED Task] targetType \"%s\" and id \"%d\"", EnumToString::TaskTargetType(task.targetType),
           task.property_id);

  this->incoming_message.reset();
  this->data_get_queue.pop_front();

  // TODO: Retry?
}

void SECTouchComponent::fill_get_queue_with_fans() {
  if (this->recursive_update_ids[0] == 0) {
    ESP_LOGE(TAG, "No fan is registered for recursive updates");
    this->mark_failed();
    return;
  }

  for (size_t i = 0; i < this->recursive_update_ids.size(); i++) {
    int id = this->recursive_update_ids[i];
    if (id == 0) {
      return;
    }

    this->data_get_queue.push_back(GetDataTask::create(TaskTargetType::LEVEL, id));
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
  this->data_set_queue.push_back(std::move(task));
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
  ESP_LOGD(TAG_UART, "SendMessageAck sended");
}

void SECTouchComponent::process_data_for_current_get_queue_item() {
  auto &task_ptr = this->data_get_queue.front();

  ESP_LOGD(TAG, "process_data");
  ESP_LOGD(TAG, "  [process_data] Incoming message buffer for get task(%d) targetType %s and id %d", COMMANDID_GET,
           EnumToString::TaskTargetType(task_ptr->targetType), task_ptr->property_id);

  ESP_LOGD(TAG, "  [process_data] buffer %s", this->incoming_message.buffer);

  // VALIDATION
  if (static_cast<uint8_t>(this->incoming_message.buffer[0]) != STX) {
    ESP_LOGE(TAG, "  [process_data] Expected STX at index 0, but received %d. Task Failed",
             this->incoming_message.buffer[0]);
    this->mark_current_get_queue_item_as_failed();
    return;
  }

  int last_incoming_message_index = this->incoming_message.buffer_index;
  // buffer_index - 1;  // -1 because the current index is the one that should be written next
  if (static_cast<uint8_t>(this->incoming_message.buffer[last_incoming_message_index]) != ETX) {
    ESP_LOGE(TAG, "  [process_data] Expected ETX at index %d, but received  %d . Task Failed",
             last_incoming_message_index, this->incoming_message.buffer[last_incoming_message_index]);
    this->mark_current_get_queue_item_as_failed();
    return;
  }

  ESP_LOGD(TAG, "  [process_data] incoming.returned_id: %s, incoming.extracted_value: %s",
           this->incoming_message.get_returned_id().c_str(), this->incoming_message.get_returned_value().c_str());

  if (this->incoming_message.returned_id_is_empty()) {
    ESP_LOGE(TAG, "  [process_data]returned_id is empty. Task Failed");
    this->mark_current_get_queue_item_as_failed();
    return;
  }
  if (this->incoming_message.returned_value_is_empty()) {
    ESP_LOGE(TAG, "  [process_data returned_value is empty. Task Failed");
    this->mark_current_get_queue_item_as_failed();
    return;
  }

  int returned_id = this->incoming_message.get_returned_id_as_int();
  int returned_value = this->incoming_message.get_returned_value_as_int();

  ESP_LOGD(TAG, "  [process_data] returned_id: %d, extracted_value: %d", returned_id, returned_value);

  if (this->data_get_queue.front()->property_id != returned_id) {
    ESP_LOGE(TAG, "  [process_data] ID mismatch. Task Failed");
    this->mark_current_get_queue_item_as_failed();
    return;
  }

  // SEND UPDATE!
  this->notify_recursive_update_listeners(returned_id, returned_value);

  ESP_LOGD(TAG, "  [process_data] Task with targetType %s and id %d processed. New Value is %d",
           EnumToString::TaskTargetType(task_ptr->targetType), task_ptr->property_id, returned_value);

  this->data_get_queue.pop_front();
  this->incoming_message.reset();
}

}  // namespace sec_touch
}  // namespace esphome