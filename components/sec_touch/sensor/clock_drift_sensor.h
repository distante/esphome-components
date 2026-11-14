#pragma once

#include "esphome/core/defines.h"
#ifdef USE_TIME

#include <string>
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "../sec_touch.h"

namespace esphome {
namespace sec_touch {

/**
 * Seconds the panel clock is ahead of `time_` (negative: behind), updated on every
 * global settings poll.
 */
class SecTouchClockDriftSensor : public sensor::Sensor, public Component, public Parented<SECTouchComponent> {
 public:
  void set_time(time::RealTimeClock *time) { this->time_ = time; }
  void setup() override;
  void dump_config() override;

 protected:
  void on_panel_clock_(const std::string &clock);

  time::RealTimeClock *time_{nullptr};
  // the poll reads the date right before the clock, see setup(); consumed by on_panel_clock_()
  std::string panel_date_;
  uint32_t panel_date_ms_{0};
};

}  // namespace sec_touch
}  // namespace esphome

#endif  // USE_TIME
