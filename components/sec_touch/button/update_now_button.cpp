#include "update_now_button.h"

namespace esphome {
namespace sec_touch {

void UpdateNowButton::press_action() { this->parent_->update_now(true); }

}  // namespace sec_touch
}  // namespace esphome
