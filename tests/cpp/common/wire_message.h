#pragma once

// Test helpers shared by the sec_touch suites. Not a suite: PlatformIO only runs folders named test_<name>.

#include <string>

#include "sec_touch/data_message.h"

namespace sec_touch_test {

/// Builds [STX]body[ETX], where body uses '\t' for TAB.
inline std::string wire_message(const std::string &body) {
  return std::string(1, static_cast<char>(esphome::sec_touch::WIRE_STX)) + body +
         std::string(1, static_cast<char>(esphome::sec_touch::WIRE_ETX));
}

}  // namespace sec_touch_test
