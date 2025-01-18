#include "sec_touch_fan.h"
#include "../sec_touch.h"
#include "../_definitions.h"

namespace esphome {
namespace sec_touch {

// Constructor
SecTouchFan::SecTouchFan(SECTouchComponent *parent, int level_id, int label_id)
    : level_id(level_id), label_id(label_id), parent(parent) {
  // LEVEL HANDLER
  this->parent->register_recursive_update_listener(this->level_id, [this](int property_id, int new_speed) {
    // if (new_speed > 6) {
    //   ESP_LOGW(TAG, "Value %d is not yet handled for fan level_id %d", new_speed, this->level_id);
    //   this->status_set_warning("value not yet handled, value will be 0");
    //   this->turn_off();
    //   this->publish_state();
    //   return;
    // }
    text_sensor::TextSensor *level_text_sensor = this->parent->get_text_sensor(this->level_id).value_or(nullptr);
    if (level_text_sensor != nullptr) {
      level_text_sensor->publish_state(std::to_string(this->level_id).c_str());
    } else {
      ESP_LOGV(TAG, "No text sensor found for level_id %d", this->level_id);
    }

    if (new_speed == 0) {
      this->state = 0;
    } else {
      this->state = 1;
    }
    this->speed = new_speed;

    ESP_LOGI(TAG, "Setting level value for fan with property_id %d to %d (state %d)", property_id, new_speed,
             this->state);
    this->publish_state();
  });

  // LABEL HANDLER
  this->parent->register_manual_update_listener(this->label_id, [this](int property_id, int new_value) {
    text_sensor::TextSensor *label_text_sensor = this->parent->get_text_sensor(this->label_id).value_or(nullptr);
    if (label_text_sensor == nullptr) {
      ESP_LOGV(TAG, "No text sensor found for label_id %d", this->label_id);
      return;
    }

    if (new_value < 0 || new_value >= NAME_MAPPING_COUNT) {
      ESP_LOGW(TAG, "Value %d is not yet handled for fan label_id %d (level_id %d)", new_value, this->label_id,
               this->level_id);
      return;
    }

    auto new_label = NAME_MAPPING[new_value];

    const std::string &current_label = label_text_sensor->get_state();  // Get the current state of the text sensor
    if (current_label == new_label) {
      ESP_LOGD(TAG, "Value is already up-to-date: %s", new_value);
      return;  // Do not publish if the value is the same
    }

    label_text_sensor->publish_state(new_label);
  });
}

void SecTouchFan::control(const fan::FanCall &call) {
  ESP_LOGD(TAG, "Control called");
  if (call.get_state().has_value()) {
    ESP_LOGD(TAG, "Setting state to %d", *call.get_state());
    this->state = *call.get_state();
  }

  if (call.get_speed().has_value()) {
    ESP_LOGD(TAG, "Setting speed to %d", *call.get_speed());
    this->speed = *call.get_speed();
  }

  if (this->state == 0) {
    this->parent->add_set_task(SetDataTask::create(TaskTargetType::LEVEL, this->level_id, std::to_string(0).c_str()));
  } else {
    this->parent->add_set_task(
        SetDataTask::create(TaskTargetType::LEVEL, this->level_id, std::to_string(this->speed).c_str()));
  }

  this->publish_state();
}

// Print method for debugging
void SecTouchFan::printConfig() {
  ESP_LOGCONFIG(TAG, "Level ID: %d, Label ID: %d, Fan Level Value: %d", this->level_id, this->label_id, this->speed);
}

}  // namespace sec_touch
}  // namespace esphome
