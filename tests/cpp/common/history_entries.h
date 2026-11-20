#pragma once

// Test helpers shared by the sec_touch_sniffer suites. Not a suite: PlatformIO only runs folders named test_<name>.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "sec_touch_sniffer/history_pool.h"

namespace sec_touch_test {

/// A previous value as `{first seen, raw value}`.
using HistoryEntry = std::pair<uint32_t, std::string>;

/// The ID's previous values, newest first.
inline std::vector<HistoryEntry> history_of(const esphome::sec_touch::HistoryPool &pool, uint8_t id) {
  std::vector<HistoryEntry> entries;
  pool.for_each_newest_first(id, [&](uint32_t first_seen_s, const char *bytes, uint8_t len) {
    entries.emplace_back(first_seen_s, std::string(bytes, len));
  });
  return entries;
}

}  // namespace sec_touch_test
