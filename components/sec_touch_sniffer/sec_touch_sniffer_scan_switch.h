#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/helpers.h"
#include "sec_touch_sniffer.h"

namespace esphome {
namespace sec_touch {

class SecTouchSnifferScanSwitch : public switch_::Switch, public Parented<SecTouchSniffer> {
 public:
  SecTouchSnifferScanSwitch() = default;

 protected:
  /// On starts a discover over the default range, off stops; both go through the same request path as the page.
  void write_state(bool state) override { this->parent_->request_scan(state); }
};

}  // namespace sec_touch
}  // namespace esphome
