#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_scan_goal.
// ../sec_touch/header_reader.py parses it for page_bundle.py at every build: keep its declarations in the forms that
// file's docstring lists.
//
// The sniffer goals' scans (.specs/sniffer.md, "Sniffer goals" and "Active scan"): which IDs a run queries, in what
// order, and the start request that picks them ("Device/page contract", start parameters).

#include <cstdint>
#include <initializer_list>
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

/// A set of property IDs, one bit per record slot. IDs outside 0..MAX_PROPERTY_ID are never in it.
class IdSet {
 public:
  constexpr IdSet() = default;
  /// The set of `ids`: a duplicate counts once, an ID outside 0..MAX_PROPERTY_ID is dropped.
  constexpr IdSet(std::initializer_list<int> ids) {
    for (int id : ids) {
      this->add(id);
    }
  }
  constexpr void add(int id) {
    if (id < 0 || id > MAX_PROPERTY_ID) {
      return;
    }
    this->words_[id / BITS_PER_WORD] |= uint32_t{1} << (id % BITS_PER_WORD);
  }
  constexpr bool contains(int id) const {
    if (id < 0 || id > MAX_PROPERTY_ID) {
      return false;
    }
    return ((this->words_[id / BITS_PER_WORD] >> (id % BITS_PER_WORD)) & 1) != 0;
  }
  /// Calls `f(id)` for each ID in the set, ascending.
  template<typename F> constexpr void for_each(F f) const {
    for (int id = 0; id <= MAX_PROPERTY_ID; id++) {
      if (this->contains(id)) {
        f(id);
      }
    }
  }
  /// How many IDs are in the set.
  constexpr int count() const {
    int n = 0;
    this->for_each([&n](int) { n++; });
    return n;
  }
  constexpr bool operator==(const IdSet &other) const {
    for (size_t i = 0; i < WORDS; i++) {
      if (this->words_[i] != other.words_[i]) {
        return false;
      }
    }
    return true;
  }

 protected:
  static constexpr size_t BITS_PER_WORD = 32;
  static constexpr size_t WORDS = PROPERTY_SLOTS / BITS_PER_WORD;
  static_assert(PROPERTY_SLOTS % BITS_PER_WORD == 0, "IdSet words must cover the record slots exactly");
  uint32_t words_[WORDS]{};
};

/// One scan run: its goal and, for discover, either its range or, for a list run (a narrowed capture), its ID list
/// held as a set. Check ignores both.
struct ScanRun {
  ScanGoal goal;
  uint8_t from;
  uint8_t to;
  /// A list run: `ids` picks the IDs instead of `from`/`to`.
  bool listed{false};
  IdSet ids{};
};

/// Most distinct IDs a list run takes, so a start's URI stays under ESP-IDF's 512-byte CONFIG_HTTPD_MAX_URI_LEN.
constexpr int MAX_SCAN_LIST_IDS = 100;

static_assert(MAX_PROPERTY_ID <= UINT8_MAX, "a ScanRun range bound no longer fits in uint8_t");

/// The discover range when a start names none.
constexpr uint8_t DEFAULT_DISCOVER_FROM = 1;
constexpr uint8_t DEFAULT_DISCOVER_TO = MAX_PROPERTY_ID;
constexpr ScanRun DEFAULT_DISCOVER_RUN{ScanGoal::SCAN_GOAL_DISCOVER, DEFAULT_DISCOVER_FROM, DEFAULT_DISCOVER_TO};
/// The check run. Its range is unused; it carries the discover default.
constexpr ScanRun CHECK_RUN{ScanGoal::SCAN_GOAL_CHECK, DEFAULT_DISCOVER_FROM, DEFAULT_DISCOVER_TO};

/// A discover list run of `ids`.
constexpr ScanRun list_run(const IdSet &ids) {
  return ScanRun{ScanGoal::SCAN_GOAL_DISCOVER, DEFAULT_DISCOVER_FROM, DEFAULT_DISCOVER_TO, true, ids};
}

/// No ID: past a run's last ID, or a run with no IDs.
constexpr int NO_SCAN_ID = -1;

/// The goals as the device names them in `scan.goal` and the start's `goal` parameter; the page reads them too.
constexpr const char *SCAN_GOAL_DISCOVER_NAME = "discover";
constexpr const char *SCAN_GOAL_CHECK_NAME = "check";

/// The goal as the page names it.
constexpr const char *scan_goal_name(ScanGoal goal) {
  return goal == ScanGoal::SCAN_GOAL_CHECK ? SCAN_GOAL_CHECK_NAME : SCAN_GOAL_DISCOVER_NAME;
}

/// Whether `run` queries `id`. Check covers every record slot, so its IDs are exactly the known ones.
constexpr bool scan_queries(const ScanRun &run, int id) {
  if (run.goal == ScanGoal::SCAN_GOAL_CHECK) {
    return id >= 0 && id <= MAX_PROPERTY_ID && is_known_property(id);
  }
  if (run.listed) {
    return run.ids.contains(id) && !is_known_property(id);
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

/// Why a start request is a 400. Nothing starts.
enum class ScanStartError : uint8_t {
  SCAN_START_OK,
  /// `from`/`to` partial, non-numeric, above MAX_PROPERTY_ID, or from > to.
  SCAN_START_BAD_RANGE,
  /// `ids` malformed, an ID above MAX_PROPERTY_ID, or more than MAX_SCAN_LIST_IDS distinct IDs.
  SCAN_START_BAD_IDS,
  /// `ids` together with `from` or `to`.
  SCAN_START_IDS_WITH_RANGE,
};

/// A parsed `POST /sniffer/scan/start` query. `run` is meaningful only without an error.
struct ScanStart {
  ScanStartError error;
  ScanRun run;
};

namespace scan_goal_detail {

/// `text` read as a property ID (1+ ASCII digits, at most MAX_PROPERTY_ID), or NO_SCAN_ID.
constexpr int property_id_from(std::string_view text) {
  const std::optional<uint64_t> value = parse_bounded_uint(text, MAX_PROPERTY_ID);
  return value.has_value() ? static_cast<int>(*value) : NO_SCAN_ID;
}

/// The discover list run of an `ids` value, or SCAN_START_BAD_IDS.
inline ScanStart list_run_from(std::string_view ids_text) {
  ScanRun run = list_run(IdSet{});
  const bool parsed = for_each_list_item(ids_text, [&run](std::string_view item) {
    const int id = property_id_from(item);
    if (id == NO_SCAN_ID) {
      return false;
    }
    run.ids.add(id);
    return run.ids.count() <= MAX_SCAN_LIST_IDS;
  });
  if (!parsed) {
    return ScanStart{ScanStartError::SCAN_START_BAD_IDS, DEFAULT_DISCOVER_RUN};
  }
  return ScanStart{ScanStartError::SCAN_START_OK, run};
}

}  // namespace scan_goal_detail

/// The start request in `uri`'s query: `goal` is `check` or else discover. Discover takes either `ids`, a
/// comma-separated list of IDs held as a set (1 to MAX_SCAN_LIST_IDS distinct IDs), or `from`/`to`, both missing
/// (DEFAULT_DISCOVER_RUN) or both IDs with from <= to. Check ignores `ids`, `from` and `to`, so they can't make it
/// invalid.
inline ScanStart scan_start_from_uri(std::string_view uri) {
  if (query_value(uri, "goal") == scan_goal_name(ScanGoal::SCAN_GOAL_CHECK)) {
    return ScanStart{ScanStartError::SCAN_START_OK, CHECK_RUN};
  }
  const auto ids_text = query_value(uri, "ids");
  const auto from_text = query_value(uri, "from");
  const auto to_text = query_value(uri, "to");
  if (ids_text.has_value()) {
    if (from_text.has_value() || to_text.has_value()) {
      return ScanStart{ScanStartError::SCAN_START_IDS_WITH_RANGE, DEFAULT_DISCOVER_RUN};
    }
    return scan_goal_detail::list_run_from(*ids_text);
  }
  if (!from_text.has_value() && !to_text.has_value()) {
    return ScanStart{ScanStartError::SCAN_START_OK, DEFAULT_DISCOVER_RUN};
  }
  const ScanStart bad_range{ScanStartError::SCAN_START_BAD_RANGE, DEFAULT_DISCOVER_RUN};
  if (!from_text.has_value() || !to_text.has_value()) {
    return bad_range;
  }
  const int from = scan_goal_detail::property_id_from(*from_text);
  const int to = scan_goal_detail::property_id_from(*to_text);
  if (from == NO_SCAN_ID || to == NO_SCAN_ID || from > to) {
    return bad_range;
  }
  return ScanStart{ScanStartError::SCAN_START_OK,
                   ScanRun{ScanGoal::SCAN_GOAL_DISCOVER, static_cast<uint8_t>(from), static_cast<uint8_t>(to)}};
}

}  // namespace sec_touch
}  // namespace esphome
