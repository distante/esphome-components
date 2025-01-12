#include "esphome/components/fan/fan.h"
#include "sec_touch_fan.h"
#include "../sec_touch.h"

namespace esphome {
namespace sec_touch {

// Constructor
SecTouchFan::SecTouchFan(SECTouchComponent *parent, int level_id, int label_id)
    : level_id(level_id), label_id(label_id), parent(parent) {
  this->parent->register_recursive_update_listener(this->level_id, [this](int property_id, int new_speed) {
    // if (new_speed > 6) {
    //   ESP_LOGW(TAG, "Value %d is not yet handled for fan level_id %d", new_speed, this->level_id);
    //   this->status_set_warning("value not yet handled, value will be 0");
    //   this->turn_off();
    //   this->publish_state();
    //   return;
    // }

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

  if (this->speed > 0 && this->state == 1) {
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
