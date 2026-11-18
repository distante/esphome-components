#pragma once

// Test helpers shared by the sec_touch suites. Not a suite: PlatformIO only runs folders named test_<name>.

#include <string>

namespace sec_touch_test {

// Wire bytes, as in sec_touch/_definitions.h.
constexpr char STX = 0x02;
constexpr char ETX = 0x0A;

/// Builds [STX]body[ETX], where body uses '\t' for TAB.
inline std::string wire_message(const std::string &body) { return std::string(1, STX) + body + std::string(1, ETX); }

}  // namespace sec_touch_test
