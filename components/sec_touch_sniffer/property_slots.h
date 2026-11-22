#pragma once

// Plain C++, no ESPHome includes: shared by the property store and the history pool.

#include <cstddef>
#include <cstdint>

#include "../sec_touch/data_message.h"

namespace esphome {
namespace sec_touch {

/// The highest property ID with a slot. Messages for higher IDs are dropped.
constexpr int MAX_PROPERTY_ID = 255;
static_assert(MAX_PROPERTY_ID <= UINT8_MAX, "the history pool stores a property ID in a byte");
/// Property IDs the sniffer records: one slot per ID 0..MAX_PROPERTY_ID.
constexpr size_t PROPERTY_SLOTS = MAX_PROPERTY_ID + 1;

/// Longest raw value kept, never truncated: the longest value the hub's frame buffer can deliver. The history pool
/// keeps no longer value either.
constexpr size_t RAW_VALUE_CAP = MAX_DATA_VALUE_LEN;
static_assert(RAW_VALUE_CAP <= UINT8_MAX,
              "PropertyFields::raw_len and HistoryPool entries hold a raw length in a byte");

}  // namespace sec_touch
}  // namespace esphome
