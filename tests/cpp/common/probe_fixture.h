#pragma once

// The write probe's test values, shared by the sniffer suites. Not a suite: PlatformIO only runs folders named
// test_<name>.

#include <cstdint>
#include <string>

namespace sec_touch_test {

/// An unknown property.
constexpr uint8_t PROBE_ID = 77;
/// What the panel holds before the probe.
inline const std::string ORIGINAL = "01";
/// What the probe writes.
inline const std::string VALUE = "03";
/// A value that is neither the original nor the probed one.
inline const std::string THIRD = "02";

}  // namespace sec_touch_test
