#include "sec_touch.h"
#include "XModemCRC.h"
#include "esphome/core/log.h"
#include "_definitions.h"

namespace esphome {
namespace sec_touch {

static const char *const TAG = "sec-touch";

SECTouchComponent::SECTouchComponent() {}

void SECTouchComponent::setup() {
  ESP_LOGI(TAG, "SEC-Touch setup initializing.");

  this->fill_get_queue_with_fans();
  this->reset_incoming_message();

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

void SECTouchComponent::loop() {
  if (!this->available()) {  // We are not waiting any response
    // If no other task is running, lets run all set tasks
    if (this->current_running_task_type == TaskType::NONE && this->data_set_queue.size() > 0) {
      this->process_set_queue();
    }
    return;
  }

  // We have send some data and now we are waiting for the response
  uint8_t peakedData;
  this->peek_byte(&peakedData);

  if (peakedData == NOISE && this->incoming_message.buffer_index == -1) {
    ESP_LOGD(TAG, "  Discarding noise byte");
    this->read_byte(&peakedData);  // Discard the noise
    return;
  }

  if (this->current_running_task_type == TaskType::GET) {
    this->handle_uart_input_for_get_queue();
    this->current_running_task_type = TaskType::NONE;
    return;
  }

  if (this->current_running_task_type == TaskType::SET) {
    while (this->available()) {
      uint8_t data;
      this->read_byte(&data);
      this->store_data_to_incoming_message(data);
    }

    ESP_LOGD(TAG, "  Received SET Response %s", this->incoming_message.buffer);
    if (this->incoming_message.buffer_index == 2 && this->incoming_message.buffer[0] == STX &&
        this->incoming_message.buffer[1] == ACK && this->incoming_message.buffer[2] == ETX) {
      ESP_LOGD(TAG, "  SET was successful");
    } else {
      ESP_LOGE(TAG, "  SET was not successful");
    }
    this->data_set_queue.pop();
    this->reset_incoming_message();
    this->current_running_task_type = TaskType::NONE;
  }
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
  ESP_LOGD(TAG, "[Loop] Data available--------");
  int current_index = -1;

  int total_tabs = 0;
  int total_etx = 0;
  int total_stx = 0;

  bool ack_already_failed = false;

  while (this->available()) {
    uint8_t data;
    this->read_byte(&data);

    ESP_LOGD(TAG, "  Byte received: %d", data);

    bool start_of_ack_message = this->incoming_message.buffer_index == 0;

    if (start_of_ack_message && data != ACK) {
      if (ack_already_failed) {
        ESP_LOGW(TAG,
                 "Starting to store message but the data is not ACK in the second attempt. Discarding all data: %d",
                 data);
        this->reset_incoming_message();
        return;
      }

      ack_already_failed = true;
      ESP_LOGW(TAG, "Starting to store message but the data is not ACK. Discarding data: %d", data);
      continue;
    } else if (!start_of_ack_message && data == NOISE) {
      ESP_LOGE(TAG, "  received NOISE (255) inside message discarding all data: %d", data);
      this->reset_incoming_message();
      return;
    }

    bool inside_id = total_etx == 1 && total_stx > 1 && total_tabs == 1;
    bool inside_value = total_etx == 1 && total_stx > 1 && total_tabs == 2;

    current_index = this->store_data_to_incoming_message(data);

    if (inside_id) {
      ESP_LOGD(TAG, "    saving into id");
      this->incoming_message.returned_id += data;
    } else if (inside_value) {
      ESP_LOGD(TAG, "    saving into value");
      this->incoming_message.returned_value += data;
    } else {
      ESP_LOGD(TAG, "    saving into buffer without id or value");
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

    ESP_LOGD(TAG, "  Received Data Without Request %s", this->incoming_message.buffer);
    int returned_id = std::stoi(this->incoming_message.returned_id);
    int returned_value = std::stoi(this->incoming_message.returned_value);
    ESP_LOGD(TAG, "    string incoming.returned_id: %s, incoming.extracted_value: %s",
             this->incoming_message.returned_id.c_str(), this->incoming_message.returned_value.c_str());
    ESP_LOGD(TAG, "  Received Data Without Request returned_id: %d, extracted_value: %d", returned_id, returned_value);
    ESP_LOGD(TAG, "[Loop]------------------------------------------");
    this->send_ack_message();
    this->process_data_for_current_get_queue_item();

    if (!this->data_get_queue.empty()) {
      this->update_now(false);
    }
  }
}

bool SECTouchComponent::process_set_queue() {
  // TODO: Delete  manually_process_set_queue ?
  this->manually_process_set_queue();
  return false;
}

template<typename SomeTask>
bool SECTouchComponent::process_queue(std::queue<std::unique_ptr<SomeTask>> &taskQueue,
                                      const std::string &queueLoggingName) {
  if (taskQueue.size() == 0) {
    ESP_LOGE(TAG, "No data in the given queue %s", queueLoggingName.c_str());
    return false;
  }

  this->current_running_task_type = TaskType::GET;
  ESP_LOGD(TAG, "process_queue \"%s\" with size %d", queueLoggingName.c_str(), taskQueue.size());
  // Access the smart pointer from the queue
  auto &task_ptr = taskQueue.front();

  // Dereference the smart pointer to access the actual object
  auto &task = *task_ptr;
  ESP_LOGD(TAG, "Task targetType \"%s\" and property_id \"%d\"", EnumToString::TaskTargetType(task.targetType),
           task.property_id);

  taskQueue.front()->state = TaskState::TO_BE_PROCESSED;
  this->send_get_message(task);

  return true;
}

int SECTouchComponent::store_data_to_incoming_message(uint8_t data) { return this->incoming_message.store_data(data); }

void SECTouchComponent::reset_incoming_message() {
  this->incoming_message.buffer_index = -1;
  this->incoming_message.returned_id = "";
  this->incoming_message.returned_value = "";
  memset(this->incoming_message.buffer, 0, sizeof(this->incoming_message.buffer));
}

void SECTouchComponent::mark_current_get_queue_item_as_failed() {
  auto &task_ptr = this->data_get_queue.front();
  auto &task = *task_ptr;
  ESP_LOGD(TAG, "[FAILED Task] targetType \"%s\" and id \"%d\"", EnumToString::TaskTargetType(task.targetType),
           task.property_id);

  this->reset_incoming_message();
  this->data_get_queue.pop();

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

    this->data_get_queue.push(GetDataTask::create(TaskTargetType::LEVEL, id));
  }
}

void SECTouchComponent::register_recursive_update_listener(int property_id, UpdateCallbackListener listener) {
  ESP_LOGD(TAG, "register_recursive_update_listener for property_id %d", property_id);
  this->recursive_update_listeners[property_id] = std ::move(listener);
  this->recursive_update_ids.push_back(property_id);
}

void SECTouchComponent::add_set_task(std::unique_ptr<SetDataTask> task) {
  ESP_LOGD(TAG, "add_set_task");
  this->data_set_queue.push(std::move(task));
}

void SECTouchComponent::update_now(bool fill_get_queue) {
  ESP_LOGD(TAG, "update_now");
  if (this->available()) {
    ESP_LOGD(TAG, "Data available");
  }
  this->process_queue(this->data_get_queue, "data_get_queue");
}

void SECTouchComponent::manually_process_set_queue() {
  ESP_LOGD(TAG, "manually_process_set_queue size %d", this->data_set_queue.size());

  if (this->data_set_queue.size() == 0) {
    ESP_LOGE(TAG, "manually_process_set_queue No data in the given queue");
    return;
  }

  this->current_running_task_type = TaskType::SET;

  ESP_LOGD(TAG, "manually_process_set_queue with size %d", this->data_set_queue.size());
  // Access the smart pointer from the queue
  auto &task_ptr = this->data_set_queue.front();

  // Dereference the smart pointer to access the actual object
  auto &task = *task_ptr;
  ESP_LOGD(TAG, "manually_process_set_queue Task targetType \"%s\" and property_id \"%d\"",
           EnumToString::TaskTargetType(task.targetType), task.property_id);

  this->data_set_queue.front()->state = TaskState::TO_BE_PROCESSED;
  this->send_set_message(task);
}

void SECTouchComponent::send_get_message(GetDataTask &task) {
  ESP_LOGD(TAG, "send_get_message");

  std::array<char, 64> message_buffer;

  int len = snprintf(message_buffer.data(), message_buffer.size(), "%c%d%c%d%c", STX, COMMANDID_GET, TAB,
                     task.property_id, TAB);
  unsigned short crc = GetXModemCRC(message_buffer.data(), len);
  len += snprintf(message_buffer.data() + len, message_buffer.size() - len, "%u%c", crc, ETX);

  ESP_LOGD(TAG, "  buffer %s", message_buffer.data());
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

  ESP_LOGD(TAG, "  [process_data] Buffer contains an start and end byte");

  ESP_LOGD(TAG, "  [process_data] incoming.returned_id: %s, incoming.extracted_value: %s",
           this->incoming_message.returned_id.c_str(), this->incoming_message.returned_value.c_str());

  if (this->incoming_message.returned_id.empty()) {
    ESP_LOGE(TAG, "  [process_data]returned_id is empty. Task Failed");
    this->mark_current_get_queue_item_as_failed();
    return;
  }
  if (this->incoming_message.returned_value.empty()) {
    ESP_LOGE(TAG, "  [process_data returned_value is empty. Task Failed");
    this->mark_current_get_queue_item_as_failed();
    return;
  }

  int returned_id = std::stoi(this->incoming_message.returned_id);
  int returned_value = std::stoi(this->incoming_message.returned_value);

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

  this->data_get_queue.pop();
  this->reset_incoming_message();
}

}  // namespace sec_touch
}  // namespace esphome