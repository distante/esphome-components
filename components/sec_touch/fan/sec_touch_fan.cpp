#include "sec_touch_fan.h"
#include "../sec_touch.h"
#include "../_definitions.h"

namespace esphome {
namespace sec_touch {

const char *const SecTouchFan::TAG = "sec_touch.fan";

SecTouchFan::SecTouchFan(SECTouchComponent *parent, int level_id, int label_id)
    : level_id(level_id), label_id(label_id), parent(parent) {
  this->add_on_state_callback([this]() { this->update_label_mode(); });

  this->parent->register_recursive_update_listener(this->level_id, [this](int property_id, int real_speed_from_device) {
    // A newer SET task for this level is already queued — this echo is from a
    // superseded command and would briefly revert the UI to a stale state.
    if (this->parent->has_pending_set_task_for(property_id)) {
      ESP_LOGD(TAG, "Skipping stale echo for property_id %d (value %d) — newer SET pending", property_id,
               real_speed_from_device);
      return;
    }

    if (real_speed_from_device < OFF_LEVEL ||
        (real_speed_from_device > MAX_LEVEL && real_speed_from_device != NOT_CONNECTED_LEVEL)) {
      ESP_LOGW(TAG, "Ignoring out-of-range speed %d for property_id %d", real_speed_from_device, property_id);
      return;
    }

    ESP_LOGD(TAG, "New Real Speed from device for property_id %d (speed %d)", property_id, real_speed_from_device);

    FanModeEnum::FanMode mode_from_hardware = SecTouchFan::calculate_mode_from_speed(real_speed_from_device);
    std::string_view mode_from_hardware_str = FanModeEnum::to_string(mode_from_hardware);

    bool needs_preset_publish = false;
    auto current_preset = this->get_preset_mode();
    if (current_preset.empty() || current_preset != mode_from_hardware_str.data()) {
      ESP_LOGD(TAG, "Preset mode changed to %s", mode_from_hardware_str.data());
      // set_preset_mode_ will validate and store a pointer into traits
      this->set_preset_mode_(mode_from_hardware_str.data());
      needs_preset_publish = true;
    }

    bool need_speed_publish = this->assign_new_speed_if_needed(real_speed_from_device);

    if (!need_speed_publish && !needs_preset_publish) {
      auto log_p = this->get_preset_mode();
      ESP_LOGD(TAG, "No update needed for fan with property_id %d incoming_speed=%d (state %d) (speed %d)(preset %s)",
               property_id, real_speed_from_device, this->state, this->speed,
               log_p.empty() ? "Unknown" : log_p.c_str());
      return;
    }

    ESP_LOGI(TAG, "Setting level value for fan with property_id %d to %d (state %d)", property_id,
             real_speed_from_device, this->state);

    this->publish_state();
  });

  this->parent->register_manual_update_listener(this->label_id, [this](int property_id, int new_value) {
    text_sensor::TextSensor *label_text_sensor = this->parent->get_text_sensor(this->label_id).value_or(nullptr);
    if (label_text_sensor == nullptr) {
      ESP_LOGV(TAG, "No text sensor found for label_id %d", this->label_id);
      return;
    }

    const char *new_label = label_name(new_value);
    if (new_label == nullptr) {
      ESP_LOGW(TAG, "Value %d is not yet handled for fan label_id %d (level_id %d)", new_value, this->label_id,
               this->level_id);
      return;
    }

    const std::string &current_label = label_text_sensor->get_state();  // Get the current state of the text sensor
    if (current_label == new_label) {
      ESP_LOGD(TAG, "Label is already up-to-date: %s", current_label.c_str());
      return;  // Do not publish if the value is the same
    }

    label_text_sensor->publish_state(new_label);
  });
}

bool SecTouchFan::assign_new_speed_if_needed(int real_speed_from_device) {
  if (real_speed_from_device == OFF_LEVEL) {
    if (this->state != 0) {
      this->state = 0;
      return true;
    }
    return false;
  }

  if (real_speed_from_device == NOT_CONNECTED_LEVEL) {
    if (this->state != 0 || this->speed != NOT_CONNECTED_LEVEL) {
      this->state = 0;
      this->speed = NOT_CONNECTED_LEVEL;
      return true;
    }
    return false;
  }

  if (real_speed_from_device <= MAX_NORMAL_LEVEL) {
    if (this->state != 1 || this->speed != real_speed_from_device) {
      this->state = 1;
      this->speed = real_speed_from_device;
      return true;
    }

    return false;
  }

  if (this->split_special_modes_) {
    // Burst snaps the slider to its max for visual parity with the outbound control() path;
    // other special modes leave speed untouched as they don't map to a single level.
    bool changed = false;
    if (this->state != 1) {
      this->state = 1;
      changed = true;
    }
    // Guarantee a normal slider value before publishing state=ON. Without this,
    // the first update after boot (speed=0) or after the "not connected" path (speed=NOT_CONNECTED_LEVEL)
    // would publish state=ON with a speed outside the advertised range.
    bool burst = real_speed_from_device == BURST_LEVEL;
    if (!is_normal_level(this->speed)) {
      this->speed = burst ? MAX_NORMAL_LEVEL : MIN_NORMAL_LEVEL;
      changed = true;
    } else if (burst && this->speed != MAX_NORMAL_LEVEL) {
      this->speed = MAX_NORMAL_LEVEL;
      changed = true;
    }
    return changed;
  }

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
  if (call.has_preset_mode()) {
    const char *pm = call.get_preset_mode();
    auto current = this->get_preset_mode();
    if (current.empty() || current != pm) {
      // Store the preset mode (validates and points into traits)
      this->set_preset_mode_(pm);
      new_preset_found = true;
      auto logged = this->get_preset_mode();
      ESP_LOGI(TAG, "NEW Fan preset mode: %s", logged.empty() ? pm : logged.c_str());
    }
  }

  FanCommand command;
  if (call.get_state().has_value()) {
    ESP_LOGD(TAG, "New state found in call: %d", *call.get_state());
    command.state = *call.get_state();
  }
  if (call.get_speed().has_value()) {
    ESP_LOGD(TAG, "New speed found in call: %d", *call.get_speed());
    command.speed = *call.get_speed();
  }

  // The fan's preset once the call's own preset is stored; empty while the fan has none.
  auto stored_preset = this->get_preset_mode();
  std::string_view stored_preset_sv =
      stored_preset.empty() ? std::string_view("") : std::string_view(stored_preset.c_str(), stored_preset.size());
  esphome::optional<FanModeEnum::FanMode> preset_mode = FanModeEnum::from_string(stored_preset_sv);
  if (new_preset_found) {
    command.preset_level = FanModeEnum::get_start_speed(preset_mode.value_or(FanModeEnum::FanMode::NORMAL));
  }

  FanControl decision = decide_fan_control(command, FanStatus{this->state, this->speed}, this->split_special_modes_);
  this->state = decision.on;
  this->speed = decision.speed;

  if (decision.level == OFF_LEVEL) {
    ESP_LOGI(TAG, "[State Update for %d] - Turning off", this->level_id);
    this->parent->add_set_task(
        SetDataTask::create(TaskTargetType::LEVEL, this->level_id, std::to_string(OFF_LEVEL).c_str()));
    this->publish_state();
    return;
  }

  // Clear a stale special-mode preset immediately so the UI stays consistent
  // without waiting for the device echo (which may arrive seconds later).
  bool preset_is_special = preset_mode.has_value() && *preset_mode != FanModeEnum::FanMode::NORMAL;
  if (decision.reset_preset_to_normal && preset_is_special) {
    preset_mode = FanModeEnum::FanMode::NORMAL;
    this->set_preset_mode_(FanModeEnum::to_string(*preset_mode).data());
  }

  ESP_LOGI(TAG, "[Update for %d] - [%s] target_level: %d (ha_speed: %d)", this->level_id,
           preset_mode.has_value() ? FanModeEnum::to_string(*preset_mode).data() : "Unknown", decision.level,
           this->speed);
  this->parent->add_set_task(
      SetDataTask::create(TaskTargetType::LEVEL, this->level_id, std::to_string(decision.level).c_str()));

  ESP_LOGI(TAG, "Publishing state of FAN");
  this->publish_state();
}

FanModeEnum::FanMode SecTouchFan::calculate_mode_from_speed(int speed) {
  if (speed < BURST_LEVEL || speed > SLEEP_LEVEL) {
    return FanModeEnum::FanMode::NORMAL;
  }

  if (speed == BURST_LEVEL) {
    return FanModeEnum::FanMode::BURST;
  }

  if (speed == AUTOMATIC_HUMIDITY_LEVEL) {
    return FanModeEnum::FanMode::AUTOMATIC_HUMIDITY;
  }

  if (speed == AUTOMATIC_CO2_LEVEL) {
    return FanModeEnum::FanMode::AUTOMATIC_CO2;
  }

  if (speed == AUTOMATIC_TIME_LEVEL) {
    return FanModeEnum::FanMode::AUTOMATIC_TIME;
  }

  if (speed == SLEEP_LEVEL) {
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

  if (this->speed == NOT_CONNECTED_LEVEL) {
    level_text_sensor->publish_state("Not Connected");
    return;
  }

  if (!this->has_preset_mode()) {
    level_text_sensor->publish_state("Unknown");
    return;
  }

  level_text_sensor->publish_state(this->get_preset_mode().c_str());
}

// Print method for debugging
void SecTouchFan::dump_config() {
  ESP_LOGCONFIG(TAG, "Level ID: %d, Label ID: %d, Current Fan Level: %d", this->level_id, this->label_id, this->speed);
}

}  // namespace sec_touch
}  // namespace esphome
