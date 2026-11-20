#pragma once

// Plain C++, no ESPHome includes: shared by the property store, the history pool and the text sensor builder.
// text_sensor.py reads MAX_PROPERTY_ID from this file for the config's ID range.

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace sec_touch {

/// The highest property ID with a slot. Messages for higher IDs are dropped.
constexpr int MAX_PROPERTY_ID = 255;
static_assert(MAX_PROPERTY_ID <= UINT8_MAX, "the history pool stores a property ID in a byte");
/// Property IDs the sniffer records: one slot per ID 0..MAX_PROPERTY_ID.
constexpr size_t PROPERTY_SLOTS = MAX_PROPERTY_ID + 1;

}  // namespace sec_touch
}  // namespace esphome
