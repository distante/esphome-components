#pragma once

#include "esphome/core/defines.h"
#ifdef USE_TIME

#include "esphome/components/button/button.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "../sec_touch.h"

namespace esphome {
namespace sec_touch {

class SecTouchSyncClockButton : public button::Button, public Component, public Parented<SECTouchComponent> {
 public:
  void set_time(time::RealTimeClock *time) { this->time_ = time; }

 protected:
  void press_action() override;
  void write_clock_();

  time::RealTimeClock *time_{nullptr};
};

}  // namespace sec_touch
}  // namespace esphome

#endif  // USE_TIME
