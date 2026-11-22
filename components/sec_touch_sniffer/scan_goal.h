#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_scan_goal.
// page_bundle.py reads the default discover range from this header: keep its declarations in the forms the comment at
// the top of ../sec_touch/protocol_ids.h lists.
//
// The sniffer goals' scans (.specs/sniffer.md, "Sniffer goals" and "Active scan"): which IDs a run queries, in what
// order, and the start request that picks them ("Device/page contract", start parameters).

#include <cstdint>
#include <optional>
#include <string_view>

#include "../sec_touch/known_property_meaning.h"
#include "property_slots.h"
#include "uri_query.h"

namespace esphome {
namespace sec_touch {

enum class ScanGoal : uint8_t {
  /// Queries a range minus every known property.
  SCAN_GOAL_DISCOVER,
  /// Queries every known property, registered ones included.
  SCAN_GOAL_CHECK,
};

/// One scan run: its goal and, for discover, its range. Check ignores the range.
struct ScanRun {
  ScanGoal goal;
  uint8_t from;
  uint8_t to;
};

static_assert(MAX_PROPERTY_ID <= UINT8_MAX, "a ScanRun range bound no longer fits in uint8_t");

/// The discover range when a start names none.
constexpr uint8_t DEFAULT_DISCOVER_FROM = 1;
constexpr uint8_t DEFAULT_DISCOVER_TO = MAX_PROPERTY_ID;
constexpr ScanRun DEFAULT_DISCOVER_RUN{ScanGoal::SCAN_GOAL_DISCOVER, DEFAULT_DISCOVER_FROM, DEFAULT_DISCOVER_TO};
/// The check run. Its range is unused; it carries the discover default.
constexpr ScanRun CHECK_RUN{ScanGoal::SCAN_GOAL_CHECK, DEFAULT_DISCOVER_FROM, DEFAULT_DISCOVER_TO};

/// No ID: past a run's last ID, or a run with no IDs.
constexpr int NO_SCAN_ID = -1;

/// The goal as the page names it.
constexpr const char *scan_goal_name(ScanGoal goal) { return goal == ScanGoal::SCAN_GOAL_CHECK ? "check" : "discover"; }

/// Whether `run` queries `id`. Check covers every record slot, so its IDs are exactly the known ones.
constexpr bool scan_queries(const ScanRun &run, int id) {
  if (run.goal == ScanGoal::SCAN_GOAL_CHECK) {
    return id >= 0 && id <= MAX_PROPERTY_ID && is_known_property(id);
  }
  return id >= run.from && id <= run.to && !is_known_property(id);
}

/// The first ID at or after `at` that `run` queries, NO_SCAN_ID when none is left.
constexpr int next_scan_id(const ScanRun &run, int at) {
  for (int id = at < 0 ? 0 : at; id <= MAX_PROPERTY_ID; id++) {
    if (scan_queries(run, id)) {
      return id;
    }
  }
  return NO_SCAN_ID;
}

/// Where every run of `run` begins: there is no resume.
constexpr int first_scan_id(const ScanRun &run) { return next_scan_id(run, 0); }

/// The last ID `run` queries, NO_SCAN_ID when it queries none.
constexpr int last_scan_id(const ScanRun &run) {
  for (int id = MAX_PROPERTY_ID; id >= 0; id--) {
    if (scan_queries(run, id)) {
      return id;
    }
  }
  return NO_SCAN_ID;
}

// Check derives its IDs from the record slots, so a known ID above them would never be checked.
static_assert(max_known_property_id() <= MAX_PROPERTY_ID, "a known property ID is above the record slots");

/// A parsed `POST /sniffer/scan/start` query. `valid` false is a 400: nothing starts.
struct ScanStart {
  bool valid;
  ScanRun run;
};

namespace scan_goal_detail {

/// `text` read as a property ID (1+ ASCII digits, at most MAX_PROPERTY_ID), or NO_SCAN_ID.
constexpr int property_id_from(std::string_view text) {
  const std::optional<uint64_t> value = parse_bounded_uint(text, MAX_PROPERTY_ID);
  return value.has_value() ? static_cast<int>(*value) : NO_SCAN_ID;
}

}  // namespace scan_goal_detail

/// The start request in `uri`'s query: `goal` is `check` or else discover; discover's `from`/`to` are both missing
/// (DEFAULT_DISCOVER_RUN) or both IDs with from <= to. Check ignores `from`/`to`, so they can't make it invalid.
inline ScanStart scan_start_from_uri(std::string_view uri) {
  if (query_value(uri, "goal") == scan_goal_name(ScanGoal::SCAN_GOAL_CHECK)) {
    return ScanStart{true, CHECK_RUN};
  }
  const auto from_text = query_value(uri, "from");
  const auto to_text = query_value(uri, "to");
  if (!from_text.has_value() && !to_text.has_value()) {
    return ScanStart{true, DEFAULT_DISCOVER_RUN};
  }
  constexpr ScanStart BAD_RANGE{false, DEFAULT_DISCOVER_RUN};
  if (!from_text.has_value() || !to_text.has_value()) {
    return BAD_RANGE;
  }
  const int from = scan_goal_detail::property_id_from(*from_text);
  const int to = scan_goal_detail::property_id_from(*to_text);
  if (from == NO_SCAN_ID || to == NO_SCAN_ID || from > to) {
    return BAD_RANGE;
  }
  return ScanStart{true, ScanRun{ScanGoal::SCAN_GOAL_DISCOVER, static_cast<uint8_t>(from), static_cast<uint8_t>(to)}};
}

}  // namespace sec_touch
}  // namespace esphome
