#pragma once

#include "esphome/core/component.h"
#include "esphome/components/fan/fan.h"
#include "../sec_touch.h"

namespace esphome {
namespace sec_touch {

class SecTouchFan : public Component, public fan::Fan {
  static constexpr const char *TAG = "SecTouchFan";

  /*

  FAN LEVEL OF 7 means Stosslüften
  FAN LEVEL OF 8 means Automatik Feuchte
  FAN LEVEL OF 9 means Automatik CO2
  FAN LEVEL OF 10 means Automatik Zeit
  FAN LEVEL OF 11 means Schlummer

*/

 protected:
  const int level_id;
  const int label_id;
  SECTouchComponent *parent;
  void control(const fan::FanCall &call) override;

 public:
  // Constructor
  SecTouchFan(SECTouchComponent *parent, int level_id, int label_id);
  // From Fan
  fan::FanTraits get_traits() override { return fan::FanTraits(false, true, false, 10); }

  // Print method for debugging
  void printConfig();
};

}  // namespace sec_touch
}  // namespace esphome
