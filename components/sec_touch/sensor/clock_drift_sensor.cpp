#include "clock_drift_sensor.h"
#include <cmath>
#include <cstdio>
#include "esphome/core/log.h"

namespace esphome {
namespace sec_touch {

static const char *const TAG = "sec_touch.clock";

void SecTouchClockDriftSensor::setup() {
  // Global settings are polled in registration order, so the date always arrives
  // right before the clock it belongs to.
  this->parent_->register_global_setting_string_listener(
      DATE_PROPERTY_ID, [this](int, const std::string &date) { this->panel_date_ = date; });
  this->parent_->register_global_setting_string_listener(
      CLOCK_PROPERTY_ID, [this](int, const std::string &clock) { this->on_panel_clock_(clock); });
}

void SecTouchClockDriftSensor::dump_config() { LOG_SENSOR("", "SEC-Touch Clock Drift", this); }

void SecTouchClockDriftSensor::on_panel_clock_(const std::string &clock) {
  ESPTime now = this->time_->now();
  if (!now.is_valid()) {
    ESP_LOGD(TAG, "Time source not synced yet, skipping drift");
    return;
  }

  // Use each date reading once: if it timed out, a stale date must not be paired with a fresh clock.
  std::string date = std::move(this->panel_date_);
  this->panel_date_.clear();
  if (date.empty()) {
    ESP_LOGD(TAG, "No panel date for this clock reading, skipping drift");
    return;
  }

  int day, month, year, hour, minute, second;
  if (sscanf(date.c_str(), "%2d.%2d.%2d", &day, &month, &year) != 3 ||
      sscanf(clock.c_str(), "%2d:%2d:%2d", &hour, &minute, &second) != 3) {
    ESP_LOGW(TAG, "Unexpected panel date/clock '%s' '%s'", date.c_str(), clock.c_str());
    this->publish_state(NAN);
    return;
  }
  // The date is read just before the clock, so right after midnight it may still be yesterday's.
  if (hour == 0 && minute == 0 && second < 5) {
    ESP_LOGD(TAG, "Panel clock just passed midnight, skipping drift");
    return;
  }

  ESPTime panel{};
  panel.year = 2000 + year;
  panel.month = month;
  panel.day_of_month = day;
  panel.hour = hour;
  panel.minute = minute;
  panel.second = second;
  panel.recalc_timestamp_local();
  if (panel.timestamp == -1) {
    ESP_LOGW(TAG, "Invalid panel date/clock '%s' '%s'", date.c_str(), clock.c_str());
    this->publish_state(NAN);
    return;
  }

  this->publish_state(panel.timestamp - now.timestamp);
}

}  // namespace sec_touch
}  // namespace esphome
