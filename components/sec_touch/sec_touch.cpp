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
  // bool set_processed = this->process_set_queue();

  // if (set_processed) {
  //   // If a set request was processed, wait for the next loop
  //   return;
  // }

  // this->process_get_queue();
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

bool SECTouchComponent::process_set_queue() {
  // if queue is empty return false
  // if queue is not empty
  // if first element is not sent
  // send the first element
  // wait for the ack
  // if ack is received
  // remove the first element
  // return true
  // else
  // return false

  return false;
}

bool SECTouchComponent::process_get_queue() {
  if (this->data_get_queue.size() == 0) {
    ESP_LOGD(TAG, "No data in the get queue");
    return false;  // DANGER: REMOVE THIS
    // this->fill_get_queue_with_fans();
  }

  ESP_LOGD(TAG, "process_get_queue of size %d", this->data_get_queue.size());
  // Access the smart pointer from the queue
  auto &task_ptr = this->data_get_queue.front();

  // Dereference the smart pointer to access the actual object
  auto &task = *task_ptr;
  ESP_LOGD(TAG, "Task targetType \"%s\" and id \"%d\"", EnumToString::TaskTargetType(task.targetType), task.id);

  switch (task.state) {
    case TaskState::TO_BE_SENT:
      this->data_get_queue.front()->state = TaskState::WAITING_ACK;
      this->send_get_message(task);
      break;
    case TaskState::WAITING_ACK: {
      this->wait_for_ack_of_current_get_queue_item();
      break;
    }
    case TaskState::WAITING_DATA: {
      this->wait_for_data_of_current_get_queue_item();
      break;
    }
    case TaskState::TO_BE_PROCESSED: {
      this->process_data_for_current_get_queue_item();
      break;
    }
    default: {
      ESP_LOGE(TAG, "Unknown task state %d" + static_cast<int>(task.state), "for task targetType \"%s\" and id \"%d\"",
               EnumToString::TaskTargetType(task.targetType), task.id);

      this->data_get_queue.pop();
      break;
    }
  }

  return true;
}

int SECTouchComponent::store_data_to_incoming_message(uint8_t data) {
  if (this->incoming_message.buffer_index + 1 < sizeof(this->incoming_message.buffer) - 1) {
    this->incoming_message.buffer_index++;
    this->incoming_message.buffer[this->incoming_message.buffer_index] = data;

  } else {
    ESP_LOGW(TAG, "  Buffer overflow! Discarding extra data.");
  }

  return this->incoming_message.buffer_index;
}

void SECTouchComponent::reset_incoming_message() {
  this->incoming_message.buffer_index = -1;
  this->incoming_message.returned_id = "";
  this->incoming_message.returned_value = "";
  memset(this->incoming_message.buffer, 0, sizeof(this->incoming_message.buffer));
}

void SECTouchComponent::mark_current_get_queue_item_as_failed() {
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

void SECTouchComponent::update_now(bool fill_get_queue) {
  if (this->available()) {
    ESP_LOGD(TAG, "Data available");
  }

  // if (this->data_get_queue.empty() && fill_get_queue) {
  //   ESP_LOGD(TAG, "Filling get queue with fans");
  //   this->fill_get_queue_with_fans();
  // }

  bool processed = this->process_get_queue();
  delay(16);
  if (processed) {
    this->update_now(false);
  } else {
    if (this->available()) {
      ESP_LOGE(TAG, "There is still data available !");
    }
  }
}

void SECTouchComponent::send_get_message(GetDataTask task) {
  ESP_LOGD(TAG, "send_get_message");

  std::array<char, 64> message_buffer;

  int len = snprintf(message_buffer.data(), message_buffer.size(), "%c%d%c%d%c", STX, COMMANDID_GET, TAB, task.id, TAB);
  unsigned short crc = GetXModemCRC(message_buffer.data(), len);
  len += snprintf(message_buffer.data() + len, message_buffer.size() - len, "%u%c", crc, ETX);

  this->write_array(reinterpret_cast<const uint8_t *>(message_buffer.data()), len);
}

void SECTouchComponent::send_ack_message() {
  uint8_t data[] = {STX, ACK, ETX};
  this->write_array(data, sizeof(data));
  ESP_LOGD(TAG, "SendMessageAck sended");
}

void SECTouchComponent::wait_for_ack_of_current_get_queue_item() {
  ESP_LOGD(TAG, "wait_for_ack");

  while (this->available()) {
    uint8_t data;
    this->read_byte(&data);

    // ESP_LOGD(TAG, "  [wait_for_ack] Byte received: %d", data);

    int current_index = this->store_data_to_incoming_message(data);

    if (current_index < 2) {  // we are waiting for STX ACK and ETX
      continue;
    }

    if (current_index > 2) {
      ESP_LOGE(TAG, "  [wait_for_ack] We are outside of the expected message amounts");
      break;
    }

    if (data == ETX && this->incoming_message.buffer[0] == STX && this->incoming_message.buffer[1] == ACK) {
      ESP_LOGD(TAG, "  [wait_for_ack] ACK and ETX received");
      this->data_get_queue.front()->state = TaskState::WAITING_DATA;
      this->reset_incoming_message();
      break;
    }
  }
}

void SECTouchComponent::wait_for_data_of_current_get_queue_item() {
  ESP_LOGD(TAG, "wait_for_data");

  int totalTabs = 0;

  while (this->available()) {
    uint8_t data;
    this->read_byte(&data);

    // ESP_LOGD(TAG, "  [wait_for_data] Byte received: %d", data);

    int current_index = this->store_data_to_incoming_message(data);

    if (current_index == 0) {
      if (data != STX) {
        ESP_LOGE(TAG, "  [wait_for_data] Expected STX, but received %d", data);
        this->reset_incoming_message();
        continue;
      }
    }

    if (totalTabs < 3) {
      if (data == TAB) {
        // ESP_LOGD(TAG, "  [wait_for_data] ADDED TAB");
        totalTabs++;
      }

      if (totalTabs == 1) {
        // ESP_LOGD(TAG, "  [wait_for_data] ID received");
        this->incoming_message.returned_id += data;
      } else if (totalTabs == 2) {
        // ESP_LOGD(TAG, "  [wait_for_data] VALUE received");
        this->incoming_message.returned_value += data;
      }
    }

    if (data != ETX) {  // we are waiting ETX
      continue;
    }

    this->incoming_message.buffer[current_index + 1] = '\0';  // Explicit null terminator
    ESP_LOGD(TAG, "  [wait_for_data] ETX received, null terminator was added at %d index. Message is now complete",
             this->incoming_message.buffer_index);
    this->send_ack_message();
  }

  this->data_get_queue.front()->state = TaskState::TO_BE_PROCESSED;
}

void SECTouchComponent::process_data_for_current_get_queue_item() {
  auto &task_ptr = this->data_get_queue.front();

  ESP_LOGD(TAG, "process_data");
  ESP_LOGD(TAG, "  [process_data] Incoming message buffer for get task(%d) targetType %s and id %d", COMMANDID_GET,
           EnumToString::TaskTargetType(task_ptr->targetType), task_ptr->id);

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

  if (this->data_get_queue.front()->id != returned_id) {
    ESP_LOGE(TAG, "  [process_data] ID mismatch. Task Failed");
    this->mark_current_get_queue_item_as_failed();
    return;
  }

  // SEND UPDATE!
  this->notify_recursive_update_listeners(returned_id, returned_value);

  ESP_LOGD(TAG, "  [process_data] Task with targetType %s and id %d processed. New Value is %d",
           EnumToString::TaskTargetType(task_ptr->targetType), task_ptr->id, returned_value);

  this->data_get_queue.pop();
  this->reset_incoming_message();
}

}  // namespace sec_touch
}  // namespace esphome