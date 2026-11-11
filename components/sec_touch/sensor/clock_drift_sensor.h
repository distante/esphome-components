#pragma once

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
  // the poll reads the date right before the clock, see setup()
  std::string panel_date_;
};

}  // namespace sec_touch
}  // namespace esphome
