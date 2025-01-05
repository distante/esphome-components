#include "fill_queue_button.h"

namespace esphome {
namespace sec_touch {

void FillQueueButton::press_action() { this->parent_->fill_get_queue_with_fans(); }

}  // namespace sec_touch
}  // namespace esphome
