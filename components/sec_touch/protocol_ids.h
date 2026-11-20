#pragma once

// Plain C++, no ESPHome includes: the command and property ids of the SEC-Touch protocol, shared by
// _definitions.h and the host-built tests in tests/cpp.

#include <array>

namespace esphome {
namespace sec_touch {

constexpr const int COMMANDID_SET = 32;
constexpr const int COMMANDID_GET = 32800;
constexpr const std::array<int, 6> FAN_LEVEL_IDS = {173, 174, 175, 176, 177, 178};
constexpr const std::array<int, 6> FAN_LABEL_IDS = {78, 79, 80, 81, 82, 83};

}  // namespace sec_touch
}  // namespace esphome
