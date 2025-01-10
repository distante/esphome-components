#include "process_set_button.h"

namespace esphome {
namespace sec_touch {

void ProcessSetButton::press_action() { this->parent_->manually_process_set_queue(); }

}  // namespace sec_touch
}  // namespace esphome
