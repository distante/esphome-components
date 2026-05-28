#include "sec_touch_mode_select.h"
#include "../fan/sec_touch_fan.h"
#include "esphome/core/log.h"

namespace esphome {
namespace sec_touch {

static const char *const OFF_OPTION = "Off";

SecTouchModeSelect::SecTouchModeSelect(SECTouchComponent *parent, int level_id)
    : parent_(parent), level_id_(level_id) {}

void SecTouchModeSelect::setup() {
  this->fan_ = this->parent_->get_fan(this->level_id_).value_or(nullptr);
  if (this->fan_ == nullptr) {
    ESP_LOGE(TAG, "No fan registered for level_id %d - select cannot operate", this->level_id_);
    this->mark_failed();
    return;
  }

  this->fan_->add_on_state_callback([this]() { this->sync_from_fan_(); });
  this->sync_from_fan_();
}

void SecTouchModeSelect::sync_from_fan_() {
  if (this->fan_ == nullptr) {
    return;
  }

  if (this->fan_->state == 0) {
    this->publish_state(OFF_OPTION);
    return;
  }

  // Speed 255 = "Not Connected" sentinel. Selects can only publish strings
  // from their option list, so leave the previous value untouched.
  if (this->fan_->speed == 255) {
    return;
  }

  auto preset = this->fan_->get_preset_mode();
  if (preset.empty()) {
    return;
  }

  this->publish_state(preset);
}

void SecTouchModeSelect::control(const std::string &value) {
  if (this->fan_ == nullptr) {
    ESP_LOGW(TAG, "control(%s) called but no fan resolved", value.c_str());
    return;
  }

  if (value == OFF_OPTION) {
    this->fan_->turn_off().perform();
    return;
  }

  this->fan_->turn_on().set_preset_mode(value).perform();
}

void SecTouchModeSelect::dump_config() {
  ESP_LOGCONFIG(TAG, "SEC-Touch Mode Select:");
  ESP_LOGCONFIG(TAG, "  Level ID: %d", this->level_id_);
  ESP_LOGCONFIG(TAG, "  Fan resolved: %s", this->fan_ != nullptr ? "yes" : "no");
}

}  // namespace sec_touch
}  // namespace esphome
