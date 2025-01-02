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
      this->data_get_queue.push(*labelTask);

    } else {
      ESP_LOGE(TAG, "Error while adding initial Fan to the queue");
      this->mark_failed();
    }
  }

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
  // If an set request

  // start polling the first element
  // wait for the ack
  // pull the next element
  // wait for the ack
  // repeat until all elements are polled
  // then start again from the beginning
}

//// FROM MANUEL's FILE

// const int SECTouchComponent::FAN_LEVEL_REGISTERS[SECTouchComponent::FAN_LEVEL_COUNT] = {173, 174, 175, 176, 177,
// 178};

// const int SECTouchComponent::FAN_LABEL_IDS[SECTouchComponent::LABEL_COUNT] = {78, 79, 80, 81, 82, 83};

}  // namespace sec_touch
}  // namespace esphome