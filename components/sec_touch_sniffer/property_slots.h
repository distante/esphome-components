#pragma once

// Plain C++, no ESPHome includes: shared by the property store, the history pool and the text sensor builder.
// text_sensor.py reads PROPERTY_SLOTS from this file for the config's ID range.

#include <cstddef>

namespace esphome {
namespace sec_touch {

/// Property IDs the sniffer records: one slot per ID 0..255, so an ID fits a uint8_t.
constexpr size_t PROPERTY_SLOTS = 256;
/// The highest property ID with a slot. Messages for higher IDs are dropped.
constexpr int MAX_PROPERTY_ID = PROPERTY_SLOTS - 1;

}  // namespace sec_touch
}  // namespace esphome
