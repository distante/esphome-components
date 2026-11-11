#include "sync_clock_button.h"
#ifdef USE_TIME

#include "esphome/core/log.h"

namespace esphome {
namespace sec_touch {

static const char *const TAG = "sec_touch.clock";

void SecTouchSyncClockButton::press_action() {
  ESPTime now = this->time_->now();
  if (!now.is_valid()) {
    ESP_LOGW(TAG, "Time source not synced yet, not setting the panel clock");
    return;
  }
  // The panel resets the seconds to 00 on every write, so write on the next minute boundary.
  // ponytail: the time source has second resolution, so the write lands up to ~1 s late.
  this->set_timeout("sync_clock", (60 - now.second) * 1000, [this]() { this->write_clock_(); });
}

void SecTouchSyncClockButton::write_clock_() {
  // Round instead of truncating: the timeout may fire a few ms before the boundary.
  ESPTime minute = ESPTime::from_epoch_local((this->time_->timestamp_now() + 30) / 60 * 60);
  char clock[9], date[9];
  minute.strftime(clock, sizeof(clock), "%H:%M:00");
  minute.strftime(date, sizeof(date), "%d.%m.%y");
  ESP_LOGI(TAG, "Setting panel clock to %s %s", date, clock);

  // clock first: it is the time-critical one
  this->parent_->add_set_task(SetDataTask::create(TaskTargetType::GLOBAL_SETTING, CLOCK_PROPERTY_ID, clock));
  this->parent_->add_set_task(SetDataTask::create(TaskTargetType::GLOBAL_SETTING, DATE_PROPERTY_ID, date));
}

}  // namespace sec_touch
}  // namespace esphome

#endif  // USE_TIME
