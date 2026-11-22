#include "sec_touch_summer_ventilation_switch.h"
#include "../_definitions.h"

namespace esphome {
namespace sec_touch {

const char *const SecTouchSummerVentilationSwitch::TAG = "sec_touch.switch";

SecTouchSummerVentilationSwitch::SecTouchSummerVentilationSwitch(SECTouchComponent *parent) : parent_(parent) {
  this->parent_->register_global_setting_recursive_listener(SUMMER_VENTILATION_PROPERTY_ID, [this](int property_id,
                                                                                                   int new_value) {
    // A SET we just queued hasn't echoed yet — ignore this stale GET response so the UI
    // doesn't flip back to the pre-toggle value. Mirrors the fan stale-echo guard.
    if (this->parent_->has_pending_set_task_for(property_id)) {
      ESP_LOGD(TAG, "Skipping stale echo for property_id %d (value %d) — newer SET pending", property_id, new_value);
      return;
    }

    // atoi turns SUMMER_VENTILATION_ON ("0A00") into 0 and SUMMER_VENTILATION_OFF ("0800") into 800.
    bool new_state = (new_value == 0);
    if (this->state == new_state && this->has_state()) {
      return;
    }
    ESP_LOGI(TAG, "Summer ventilation state from device: %s (raw=%d)", new_state ? "ON" : "OFF", new_value);
    this->publish_state(new_state);
  });
}

void SecTouchSummerVentilationSwitch::write_state(bool state) {
  const char *value = state ? SUMMER_VENTILATION_ON : SUMMER_VENTILATION_OFF;
  ESP_LOGI(TAG, "Setting summer ventilation to %s (raw=%s)", state ? "ON" : "OFF", value);
  this->parent_->add_set_task(
      SetDataTask::create(TaskTargetType::GLOBAL_SETTING, SUMMER_VENTILATION_PROPERTY_ID, value));
  this->publish_state(state);
}

void SecTouchSummerVentilationSwitch::dump_config() {
  ESP_LOGCONFIG(TAG, "SEC-Touch Summer Ventilation Switch:");
  ESP_LOGCONFIG(TAG, "  Property ID: %d", SUMMER_VENTILATION_PROPERTY_ID);
}

}  // namespace sec_touch
}  // namespace esphome
