#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_sniffer_state.

#include <cstddef>
#include <string>
#include <string_view>

#include "property_slots.h"
#include "property_store.h"

namespace esphome {
namespace sec_touch {

/// Longest text sensor state, in bytes.
constexpr size_t MAX_STATE_LEN = 2048;

/// Separator between `id=raw` entries.
constexpr const char *STATE_SEPARATOR = ", ";
/// Ends a cut state, before `+N more`. UTF-8 for U+2026.
constexpr const char *STATE_ELLIPSIS = "\xE2\x80\xA6";

/// Ends a cut state, after the ellipsis: `+N more`, N the listed records left out.
inline std::string state_more(size_t left_out) { return "+" + std::to_string(left_out) + " more"; }

/// `, …+N more`, without the separator when nothing was written before it. Short enough for std::string's inline
/// buffer, so building it does not touch the heap.
inline std::string state_marker(size_t left_out, bool after_entry) {
  return std::string(after_entry ? STATE_SEPARATOR : "") + STATE_ELLIPSIS + state_more(left_out);
}

/// Whether the text sensor lists a record: it has a raw value and no entity registered its ID. Records that only ever
/// failed and registered IDs are left to the page.
template<typename IsRegistered>
bool is_listed_in_state(const PropertyStore &store, int id, IsRegistered is_registered) {
  return store.has_raw_value(id) && !is_registered(id);
}

/// Writes the text sensor state into `out`: `id=raw` pairs for every listed record, in ID order, comma-separated. At
/// most MAX_STATE_LEN bytes; a cut state ends with `, …+N more`, N counting the listed records left out. `out` is
/// cleared first, so a string reserved once can be reused.
template<typename IsRegistered>
void build_sniffer_state(const PropertyStore &store, IsRegistered is_registered, std::string &out) {
  out.clear();
  size_t listed = 0;
  for (int id = 0; id <= MAX_PROPERTY_ID; id++) {
    if (is_listed_in_state(store, id, is_registered)) {
      listed++;
    }
  }

  // Each entry is written only with room for the marker that would follow it, so the marker always fits.
  size_t written = 0;
  for (int id = 0; id <= MAX_PROPERTY_ID && written < listed; id++) {
    if (!is_listed_in_state(store, id, is_registered)) {
      continue;
    }
    // `, 255=` at most: within std::string's inline buffer, so no heap.
    const std::string prefix = std::string(written == 0 ? "" : STATE_SEPARATOR) + std::to_string(id) + "=";
    const std::string_view raw = store.raw_value(id);
    const size_t left_after = listed - written - 1;
    const size_t reserve = left_after == 0 ? 0 : state_marker(left_after, /*after_entry=*/true).size();
    if (out.size() + prefix.size() + raw.size() + reserve > MAX_STATE_LEN) {
      break;
    }
    out += prefix;
    out += raw;
    written++;
  }

  if (written == listed) {
    return;
  }
  out += state_marker(listed - written, /*after_entry=*/written > 0);
}

}  // namespace sec_touch
}  // namespace esphome
