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
  bool set_processed = this->process_set_queue();

  if (set_processed) {
    // If a set request was processed, wait for the next loop
    return;
  }

  this->process_get_queue();
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
    return false;  // DANGER: REMOVE THIS
    // this->fill_get_queue_with_fans();
  }

  ESP_LOGD(TAG, "process_get_queue of size %d", this->data_get_queue.size());
  auto &task = this->data_get_queue.front();

  switch (task.state) {
    case TaskState::TO_BE_SENT:
      this->data_get_queue.front().state = TaskState::SENT_WAITING_ACK;

      ESP_LOGD(TAG, "sending data");
      this->send_get_message(task);
      /* code */
      break;
    case TaskState::SENT_WAITING_ACK: {
      ESP_LOGD(TAG, "SENT_WAITING_ACK");

      // Check if data is available
      while (this->available()) {
        uint8_t data;
        this->read_byte(&data);

        ESP_LOGD(TAG, "  [SENT_WAITING_ACK] Data received: %d", data);

        int current_index = this->store_to_incoming_buffer(data);

        if (current_index < 2) {  // we are waiting for STX ACK and NEW_LINE
          continue;
        }

        if (current_index > 2) {
          ESP_LOGE(TAG, "  [SENT_WAITING_ACK] We are outside of the expected message amounts");
          break;
        }

        // Handle specific data (e.g., ACK)
        if (data == NEW_LINE && incoming_message_buffer[0] == STX && incoming_message_buffer[1] == ACK) {
          ESP_LOGD(TAG, "  [SENT_WAITING_ACK] ACK received");
          this->data_get_queue.front().state = TaskState::SENT_WAITING_DATA;
          this->reset_incoming_buffer();
          break;
        }
      }

      break;
    }
    case TaskState::SENT_WAITING_DATA: {
      ESP_LOGD(TAG, "SENT_WAITING_DATA");

      // Check if data is available
      while (this->available()) {
        uint8_t data;
        this->read_byte(&data);

        ESP_LOGD(TAG, "  [SENT_WAITING_DATA] Data received: %d", data);

        this->store_to_incoming_buffer(data);

        if (data != NEW_LINE) {  // we are waiting NEW_LINE
          continue;
        }

        ESP_LOGD(TAG, "  [SENT_WAITING_DATA] NEW_LINE received");
      }

      ESP_LOGD(TAG, "  [SENT_WAITING_DATA] Incoming message buffer for task type %d and id %d", task.type, task.id);

      this->data_get_queue.pop();
      this->reset_incoming_buffer();

      break;
    }
    default: {
      ESP_LOGE(TAG, "Unknown task state %d" + static_cast<int>(task.state), "for task type %d and id %d", task.type,
               task.id);

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
    int levelId = FAN_LEVEL_IDS[i];
    int labelId = FAN_LABEL_IDS[i];

    SecTouchFan *fan = new SecTouchFan(levelId, labelId);

    this->fanManager.add_fan(fan);

    auto levelTask = GetDataTask::create(TaskTargetType::LEVEL, levelId);
    auto labelTask = GetDataTask::create(TaskTargetType::LABEL, labelId);

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
void SECTouchComponent::send_get_message(GetDataTask task) {
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
//// FROM MANUEL's FILE

// const int SECTouchComponent::FAN_LEVEL_REGISTERS[SECTouchComponent::FAN_LEVEL_COUNT] = {173, 174, 175, 176, 177,
// 178};

// const int SECTouchComponent::FAN_LABEL_IDS[SECTouchComponent::LABEL_COUNT] = {78, 79, 80, 81, 82, 83};

}  // namespace sec_touch
}  // namespace esphome