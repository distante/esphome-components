#pragma once

#include "esphome/components/button/button.h"
#include "../sec_touch.h"

namespace esphome {
namespace sec_touch {

class UpdateNowButton : public button::Button, public Parented<SECTouchComponent> {
 public:
  UpdateNowButton() = default;

 protected:
  void press_action() override;
};

}  // namespace sec_touch
}  // namespace esphome
