#include "sec_touch_fan.h"
#include "../sec_touch.h"
#include "../_definitions.h"

namespace esphome {
namespace sec_touch {

// Constructor
SecTouchFan::SecTouchFan(SECTouchComponent *parent, int level_id, int label_id)
    : level_id(level_id), label_id(label_id), parent(parent) {
  this->add_on_state_callback([this]() { this->update_mode(); });

  // LEVEL HANDLER
  this->parent->register_recursive_update_listener(this->level_id, [this](int property_id, int real_speed_from_device) {
    bool needs_publish = this->assign_new_speed_if_needed(real_speed_from_device);

    if (!needs_publish) {
      ESP_LOGD(TAG, "No update needed for fan with property_id %d (state %d)", property_id, this->state);
      return;
    }

    ESP_LOGI(TAG, "Setting level value for fan with property_id %d to %d (state %d)", property_id,
             real_speed_from_device, this->state);

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

bool SecTouchFan::assign_new_speed_if_needed(int real_speed_from_device) {
  if (real_speed_from_device == 0) {
    if (this->state != 0) {
      this->state = 0;
      return true;
    }
    return false;
  }

  if (real_speed_from_device == 255) {
    if (this->state != 0 || this->speed != 0) {
      this->state = 0;
      this->speed = 0;
      return true;
    }
    return false;
  }

  // REGULAR SPEEDS
  if (real_speed_from_device < 7) {
    if (this->state != 1 || this->speed != real_speed_from_device) {
      this->state = 1;
      this->speed = real_speed_from_device;
      return true;
    }

    return false;
  }

  // SPECIAL MODE SPEEDS
  if (this->state != 1 || this->speed != real_speed_from_device) {
    this->state = 1;
    this->speed = real_speed_from_device;
    return true;
  }

  return false;
}

void SecTouchFan::control(const fan::FanCall &call) {
  ESP_LOGD(TAG, "Control called");

  // Log the fan preset mode
  std::string preset_mode = call.get_preset_mode();
  ESP_LOGI("SecTouchFan", "Fan preset mode: %s", preset_mode.c_str());

  if (call.get_state().has_value()) {
    ESP_LOGD(TAG, "New state to %d", *call.get_state());
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

  ESP_LOGI(TAG, "[Update for %d] - speed: %d - state: ", this->level_id, this->speed, this->state);
  this->publish_state();
}

std::string_view SecTouchFan::get_mode_from_speed(int speed) {
  if (speed == 0) {
    return "Off";
  }

  if (speed > 0 && speed < 7) {
    return FanModeEnum::toString(FanModeEnum::FanMode::NORMAL);
  }

  if (speed == 7) {
    return FanModeEnum::toString(FanModeEnum::FanMode::BURST);
  }

  if (speed == 8) {
    return FanModeEnum::toString(FanModeEnum::FanMode::AUTOMATIC_HUMIDITY);
  }

  if (speed == 9) {
    return FanModeEnum::toString(FanModeEnum::FanMode::AUTOMATIC_CO2);
  }

  if (speed == 10) {
    return FanModeEnum::toString(FanModeEnum::FanMode::AUTOMATIC_TIME);
  }

  if (speed == 11) {
    return FanModeEnum::toString(FanModeEnum::FanMode::SLEEP);
  }

  if (speed == 255) {
    return "Not Connected";
  }

  return "Unknown";
}

void SecTouchFan::update_mode() {
  // Mode
  text_sensor::TextSensor *level_text_sensor = this->parent->get_text_sensor(this->level_id).value_or(nullptr);
  if (level_text_sensor == nullptr) {
    ESP_LOGD(TAG, "No text sensor found for level_id %d", this->level_id);
    return;
  }

  auto new_mode = this->get_mode_from_speed(this->speed);
  auto current_mode = level_text_sensor->get_state();

  if (new_mode == current_mode) {
    ESP_LOGD(TAG, "Mode is already up-to-date: %s (%d)", new_mode.c_str(), this->speed);
    return;
  }

  level_text_sensor->publish_state(std::string(new_mode));
}

// Print method for debugging
void SecTouchFan::printConfig() {
  ESP_LOGCONFIG(TAG, "Level ID: %d, Label ID: %d, Fan Level Value: %d", this->level_id, this->label_id, this->speed);
}

}  // namespace sec_touch
}  // namespace esphome
