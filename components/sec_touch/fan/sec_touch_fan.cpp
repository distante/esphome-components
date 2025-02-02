#include "sec_touch_fan.h"
#include "../sec_touch.h"
#include "../_definitions.h"

namespace esphome {
namespace sec_touch {

// Constructor
SecTouchFan::SecTouchFan(SECTouchComponent *parent, int level_id, int label_id)
    : level_id(level_id), label_id(label_id), parent(parent) {
  this->add_on_state_callback([this]() { this->update_label_mode(); });

  // LEVEL HANDLER This is the data tha comes from the real device.
  this->parent->register_recursive_update_listener(this->level_id, [this](int property_id, int real_speed_from_device) {
    ESP_LOGW(TAG, "New Real Speed from device for property_id %d (speed %d)", property_id, real_speed_from_device);

    FanModeEnum::FanMode mode_from_hardware = SecTouchFan::calculate_mode_from_speed(real_speed_from_device);
    std::string_view mode_from_hardware_str = FanModeEnum::to_string(mode_from_hardware);

    bool needs_preset_publish = false;
    if (this->preset_mode != mode_from_hardware_str) {
      ESP_LOGD(TAG, "Preset mode changed to %s", mode_from_hardware_str.data());
      this->preset_mode = std::string(mode_from_hardware_str);
      needs_preset_publish = true;
    }

    bool need_speed_publish = this->assign_new_speed_if_needed(real_speed_from_device);

    if (!need_speed_publish && !needs_preset_publish) {
      ESP_LOGD(TAG, "No update needed for fan with property_id %d (state %d) (speed %d)(preset %s)", property_id,
               this->state, this->speed, this->preset_mode.c_str());
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
      ESP_LOGD(TAG, "Label is already up-to-date: %s", current_label);
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

  bool new_preset_found = false;
  if (!call.get_preset_mode().empty()) {
    if (call.get_preset_mode() != this->preset_mode) {
      this->preset_mode = call.get_preset_mode();
      new_preset_found = true;
      ESP_LOGI("SecTouchFan", "NEW Fan preset mode: %s", this->preset_mode.c_str());
    }
  }

  auto old_state = this->state;

  if (call.get_state().has_value()) {
    ESP_LOGD(TAG, "New state to %d", *call.get_state());
    this->state = *call.get_state();
  }

  if (call.get_speed().has_value()) {
    ESP_LOGD(TAG, "Setting speed to %d", *call.get_speed());
    this->speed = *call.get_speed();
  }

  if (this->state == 0 && old_state == 1) {
    // OFF
    ESP_LOGI(TAG, "[Update for %d] - Turning off", this->level_id);
    this->parent->add_set_task(SetDataTask::create(TaskTargetType::LEVEL, this->level_id, std::to_string(0).c_str()));
    this->publish_state();
    return;
  }

  // ON
  if (new_preset_found) {
    FanModeEnum::FanMode calculated_mode =
        FanModeEnum::from_string(this->preset_mode).value_or(FanModeEnum::FanMode::NORMAL);
    if (calculated_mode == FanModeEnum::FanMode::NORMAL) {
      this->speed = 1;
    } else {
      this->speed = FanModeEnum::get_start_speed(calculated_mode);
    }
  }

  ESP_LOGI(TAG, "[Update for %d] - [%s] speed: %d", this->level_id, this->preset_mode.c_str(), this->speed);
  this->parent->add_set_task(
      SetDataTask::create(TaskTargetType::LEVEL, this->level_id, std::to_string(this->speed).c_str()));

  ESP_LOGI(TAG, "Publishing state of FAN");
  this->publish_state();
}

FanModeEnum::FanMode SecTouchFan::calculate_mode_from_speed(int speed) {
  if (speed < 7 > 11) {
    return FanModeEnum::FanMode::NORMAL;
  }

  if (speed == 7) {
    return FanModeEnum::FanMode::BURST;
  }

  if (speed == 8) {
    return FanModeEnum::FanMode::AUTOMATIC_HUMIDITY;
  }

  if (speed == 9) {
    return FanModeEnum::FanMode::AUTOMATIC_CO2;
  }

  if (speed == 10) {
    return FanModeEnum::FanMode::AUTOMATIC_TIME;
  }

  if (speed == 11) {
    return FanModeEnum::FanMode::SLEEP;
  }

  return FanModeEnum::FanMode::NORMAL;
}

void SecTouchFan::update_label_mode() {
  // Mode
  text_sensor::TextSensor *level_text_sensor = this->parent->get_text_sensor(this->level_id).value_or(nullptr);
  if (level_text_sensor == nullptr) {
    ESP_LOGD(TAG, "No text sensor found for level_id %d", this->level_id);
    return;
  }

  if (this->state == 0) {
    level_text_sensor->publish_state("Off");
    return;
  }

  if (this->speed == 255) {
    level_text_sensor->publish_state("Not Connected");
    return;
  }

  if (this->preset_mode.empty()) {
    level_text_sensor->publish_state("Unknown");
    return;
  }

  level_text_sensor->publish_state(this->preset_mode.c_str());
}

// Print method for debugging
void SecTouchFan::printConfig() {
  ESP_LOGCONFIG(TAG, "Level ID: %d, Label ID: %d, Fan Level Value: %d", this->level_id, this->label_id, this->speed);
}

}  // namespace sec_touch
}  // namespace esphome
