#include "poll_button.h"

namespace esphome {
namespace sec_touch {

void PollButton::press_action() { this->parent_->process_get_queue(); }

}  // namespace sec_touch
}  // namespace esphome
