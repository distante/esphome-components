#include "poll_button.h"

namespace esphome {
namespace sec_touch {

void PollButton::press_action() { this->parent_->Poll(); }

}  // namespace sec_touch
}  // namespace esphome
