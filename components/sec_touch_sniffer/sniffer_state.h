#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_sniffer_state.

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "property_store.h"

namespace esphome {
namespace sec_touch {

/// Longest text sensor state, in bytes.
constexpr size_t MAX_STATE_LEN = 2048;

/// Separator between `id=raw` entries.
constexpr const char *STATE_SEPARATOR = ", ";
/// Ends a cut state, before `+N more`. UTF-8 for U+2026.
constexpr const char *STATE_ELLIPSIS = "\xE2\x80\xA6";
/// Ends a cut state, after the ellipsis: how many listed records were left out.
constexpr const char *STATE_MORE_FORMAT = "+%zu more";

/// Whether the text sensor lists a record: it has a raw value and no entity registered its ID. Records that only ever
/// failed and registered IDs are left to the page.
template<typename IsRegistered>
bool is_listed_in_state(const PropertyStore &store, int id, IsRegistered is_registered) {
  return store.has_raw_value(id) && !is_registered(id);
}

/// Bytes `, …+N more` takes, without the separator when nothing was written before it.
inline size_t state_marker_len(size_t left_out, bool after_entry) {
  const int count_len = std::snprintf(nullptr, 0, STATE_MORE_FORMAT, left_out);
  return (after_entry ? std::strlen(STATE_SEPARATOR) : 0) + std::strlen(STATE_ELLIPSIS) +
         static_cast<size_t>(count_len);
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
    char prefix[8];
    const int prefix_len = std::snprintf(prefix, sizeof(prefix), "%s%d=", written == 0 ? "" : STATE_SEPARATOR, id);
    const size_t entry_len = static_cast<size_t>(prefix_len) + store.raw_len(id);
    const size_t left_after = listed - written - 1;
    const size_t reserve = left_after == 0 ? 0 : state_marker_len(left_after, /*after_entry=*/true);
    if (out.size() + entry_len + reserve > MAX_STATE_LEN) {
      break;
    }
    out.append(prefix, static_cast<size_t>(prefix_len));
    out.append(store.raw_value(id), store.raw_len(id));
    written++;
  }

  if (written == listed) {
    return;
  }
  if (written > 0) {
    out += STATE_SEPARATOR;
  }
  out += STATE_ELLIPSIS;
  char count[24];
  std::snprintf(count, sizeof(count), STATE_MORE_FORMAT, listed - written);
  out += count;
}

}  // namespace sec_touch
}  // namespace esphome
