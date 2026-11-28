#pragma once

// The fan's test values, shared by the fan suites. Not a suite: PlatformIO only runs folders named test_<name>.

#include "sec_touch/fan/fan_control.h"

namespace sec_touch_test {

/// The split_special_modes flag.
constexpr bool NOT_SPLIT = false;
constexpr bool SPLIT = true;

inline const esphome::sec_touch::FanStatus OFF_AT_3{/*on=*/false, /*speed=*/3};
inline const esphome::sec_touch::FanStatus ON_AT_4{/*on=*/true, /*speed=*/4};

}  // namespace sec_touch_test
