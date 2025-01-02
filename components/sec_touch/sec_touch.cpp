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

  ESP_LOGI(TAG, " SEC-Touch setup complete.");
}

void SECTouchComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "SEC-Touch:");
  ESP_LOGCONFIG(TAG, "  total_fan_pairs: %d", this->total_fan_pairs);

  this->fanManager.print_all_fans();

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
  auto &task = this->data_get_queue.front();

  switch (task.state) {
    case TaskState::TO_BE_SENT:
      this->data_get_queue.front().state = TaskState::WAITING_ACK;
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

int SECTouchComponent::store_to_incoming_buffer(uint8_t data) {
  // Store data in the buffer if there's space
  if (incoming_message_index < sizeof(incoming_message_buffer) - 1) {
    incoming_message_buffer[incoming_message_index] = data;
    incoming_message_index++;

  } else {
    ESP_LOGW(TAG, "  Buffer overflow! Discarding extra data.");
  }

  return incoming_message_index - 1;
}

void SECTouchComponent::reset_incoming_buffer() {
  incoming_message_index = 0;
  memset(incoming_message_buffer, 0, sizeof(incoming_message_buffer));
}

void SECTouchComponent::fill_get_queue_with_fans() {
  // Create all fans and queue the initial data requests
  for (int i = 0; i < this->total_fan_pairs; i++) {
    int level_id = FAN_LEVEL_IDS[i];
    int label_id = FAN_LABEL_IDS[i];

    SecTouchFan *fan = new SecTouchFan(level_id, label_id);

    this->fanManager.add_fan(fan);

    auto levelTask = GetDataTask::create(TaskTargetType::LEVEL, level_id);
    auto labelTask = GetDataTask::create(TaskTargetType::LABEL, label_id);

    if (levelTask && labelTask) {
      this->data_get_queue.push(*levelTask);
      // DANGER: ACTIVATE THIS
      // this->data_get_queue.push(*labelTask);

    } else {
      ESP_LOGE(TAG, "Error while adding initial Fan to the queue");
      this->mark_failed();
    }
  }
}

void SECTouchComponent::update_now(bool fill_get_queue) {
  if (this->data_get_queue.empty() && fill_get_queue) {
    ESP_LOGD(TAG, "Filling get queue with fans");
    this->fill_get_queue_with_fans();
  }

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

    int current_index = this->store_to_incoming_buffer(data);

    if (current_index < 2) {  // we are waiting for STX ACK and ETX
      continue;
    }

    if (current_index > 2) {
      ESP_LOGE(TAG, "  [wait_for_ack] We are outside of the expected message amounts");
      break;
    }

    // Handle specific data (e.g., ACK)
    if (data == ETX && incoming_message_buffer[0] == STX && incoming_message_buffer[1] == ACK) {
      ESP_LOGD(TAG, "  [wait_for_ack] ACK and ETX received");
      this->data_get_queue.front().state = TaskState::WAITING_DATA;
      this->reset_incoming_buffer();
      break;
    }
  }
}

void SECTouchComponent::wait_for_data_of_current_get_queue_item() {
  ESP_LOGD(TAG, "wait_for_data");

  while (this->available()) {
    uint8_t data;
    this->read_byte(&data);

    // ESP_LOGD(TAG, "  [wait_for_data] Byte received: %d", data);

    int current_index = this->store_to_incoming_buffer(data);

    if (current_index == 0) {
      if (data != STX) {
        ESP_LOGE(TAG, "  [wait_for_data] Expected STX, but received %d", data);
        this->reset_incoming_buffer();
        continue;
      }
    }

    if (data != ETX) {  // we are waiting ETX
      continue;
    }

    ESP_LOGD(TAG, "  [wait_for_data] ETX received, message is now complete");
    this->send_ack_message();
  }

  this->data_get_queue.front().state = TaskState::TO_BE_PROCESSED;
}

void SECTouchComponent::parse_incoming_message(const char *buffer, std::string &extracted_id,
                                               std::string &extracted_value) {
  int totalTabs = 0;

  for (size_t i = 1; i < incoming_message_index + 1; ++i) {
    // ESP_LOGD("  [process_data] incoming_message_buffer", "Byte %d: 0x%02X ('%c')", i, incoming_message_buffer[i],
    //  incoming_message_buffer[i]);
    if (totalTabs >= 3) {
      break;
    }
    if (incoming_message_buffer[i] == TAB) {
      totalTabs++;
      continue;
    }

    if (totalTabs == 1) {
      extracted_id += incoming_message_buffer[i];
    } else if (totalTabs == 2) {
      extracted_value += incoming_message_buffer[i];
    }
  }
}

void SECTouchComponent::process_data_for_current_get_queue_item() {
  auto &task = this->data_get_queue.front();

  ESP_LOGD(TAG, "process_data");
  ESP_LOGD(TAG, "  [process_data] Incoming message buffer for get task(%d) targetType %s and id %d", COMMANDID_GET,
           EnumToString::TaskTargetType(task.targetType), task.id);
  ESP_LOGD(TAG, "  [process_data] incoming_message_buffer %s", incoming_message_buffer);

  if (static_cast<uint8_t>(incoming_message_buffer[0]) != STX) {
    ESP_LOGE(TAG, "  [process_data] Expected STX at index 0, but received %d. Will try again",
             incoming_message_buffer[0]);
    this->data_get_queue.front().state = TaskState::TO_BE_SENT;
    this->reset_incoming_buffer();
    return;
  }

  int last_incoming_message_index =
      incoming_message_index - 1;  // -1 because the current index is the one that should be written next
  if (static_cast<uint8_t>(incoming_message_buffer[last_incoming_message_index]) != ETX) {
    ESP_LOGE(TAG, "  [process_data] Expected ETX at index %d, but received  %d will try again",
             last_incoming_message_index, incoming_message_buffer[last_incoming_message_index]);
    this->data_get_queue.front().state = TaskState::TO_BE_SENT;
    this->reset_incoming_buffer();
    return;
  }

  // Here the buffer is something like this `32\t173\t10\t42696`, where 173 is the id and 10 is the value, \t is the TAB
  std::string extracted_id = "", extracted_value = "";

  this->parse_incoming_message(incoming_message_buffer, extracted_id, extracted_value);

  if (extracted_id.empty() || extracted_value.empty()) {
    ESP_LOGE(TAG, "  [process_data] Extracted ID or Value is empty. Will try again");
    this->data_get_queue.front().state = TaskState::TO_BE_SENT;
    this->reset_incoming_buffer();
    return;
  }

  ESP_LOGD(TAG, "  [process_data] extracted_id: %s, extracted_value: %s", extracted_id.c_str(),
           extracted_value.c_str());

  this->fanManager.update_fan_after_task_ended(task.targetType, std::stoi(extracted_id), std::stoi(extracted_value));

  ESP_LOGD(TAG, "  [process_data] Task with targetType %s and id %d processed",
           EnumToString::TaskTargetType(task.targetType), task.id);
  this->fanManager.print_all_fans();
  // TODO: Validate Data, if it is not valid then put the task back in the queue

  this->data_get_queue.pop();
  this->reset_incoming_buffer();
}

}  // namespace sec_touch
}  // namespace esphome