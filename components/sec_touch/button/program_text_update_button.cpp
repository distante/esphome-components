#include "program_text_update_button.h"

namespace esphome {
namespace sec_touch {

void ProgramTextUpdateButton::press_action() { this->parent_->add_manual_tasks_to_queue(); }

}  // namespace sec_touch
}  // namespace esphome
