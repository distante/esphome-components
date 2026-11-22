#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_sniffer_data.
//
// The /sniffer/data contract (.specs/sniffer.md, "Device/page contract"): which records a poll gets, and their JSON.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "../sec_touch/scan_outcome.h"
#include "history_pool.h"
#include "json_chunk_writer.h"
#include "property_slots.h"
#include "property_store.h"
#include "scan_goal.h"
#include "uri_query.h"

namespace esphome {
namespace sec_touch {

/// next_record_in_delta() when no record is left.
constexpr int DELTA_END = static_cast<int>(PROPERTY_SLOTS);

/// The first record at or after `from` that changed after `since`: its `seq` is higher. With `since` 0 that is every
/// record that ever had a message or a scan query. DELTA_END when none is left.
inline int next_record_in_delta(const PropertyStore &store, int from, uint32_t since) {
  if (!store.is_allocated()) {
    return DELTA_END;
  }
  // A record's seq stays 0 until its first message or query, as the store's seq starts at 0 and goes up first.
  for (int id = from; id < DELTA_END; id++) {
    if (store.fields(id).seq > since) {
      return id;
    }
  }
  return DELTA_END;
}

/// One previous raw value, copied out of the history pool.
struct HistoryValue {
  uint32_t first_seen_s;
  uint8_t len;
  char bytes[RAW_VALUE_CAP];
};

/// A binary copy of one property record, taken under the store's lock so the JSON can be written after releasing it.
struct RecordSnapshot {
  PropertyFields fields;
  char raw[RAW_VALUE_CAP];
  /// Previous values, newest first.
  HistoryValue history[HistoryPool::MAX_PREVIOUS_PER_ID];
  uint8_t history_count;
  uint8_t id;
  bool history_on;
};

/// Copies record `id` (0..MAX_PROPERTY_ID) of an allocated store into `out`.
inline void snapshot_record(const PropertyStore &store, int id, RecordSnapshot &out) {
  out.id = static_cast<uint8_t>(id);
  out.fields = store.fields(id);
  const std::string_view raw = store.raw_value(id);
  std::memcpy(out.raw, raw.data(), raw.size());
  out.history_on = store.history().is_allocated();
  out.history_count = 0;
  store.history().for_each_newest_first(static_cast<uint8_t>(id),
                                        [&out](uint32_t first_seen_s, std::string_view value) {
                                          HistoryValue &h = out.history[out.history_count++];
                                          h.first_seen_s = first_seen_s;
                                          h.len = static_cast<uint8_t>(value.size());
                                          std::memcpy(h.bytes, value.data(), value.size());
                                        });
}

/// The query status as the page names it.
inline const char *query_status_name(const PropertyFields &f) {
  if ((f.flags & PropertyStore::FLAG_QUERIED) == 0) {
    return "unqueried";
  }
  switch (f.query_status) {
    case ScanOutcome::SCAN_OUTCOME_OK:
      return "ok";
    case ScanOutcome::SCAN_OUTCOME_NAK:
      return "nak";
    case ScanOutcome::SCAN_OUTCOME_TIMEOUT:
      return "timeout";
  }
  return "unqueried";
}

/// Writes one record object.
inline void write_record_json(JsonChunkWriter &w, const RecordSnapshot &record, bool registered) {
  const PropertyFields &f = record.fields;
  const bool seen = (f.flags & PropertyStore::FLAG_SEEN) != 0;
  const bool queried = (f.flags & PropertyStore::FLAG_QUERIED) != 0;
  const std::string_view raw(record.raw, f.raw_len);

  w.raw("{\"id\":");
  w.number(record.id);
  w.raw(",\"cmd\":");
  if (seen) {
    w.number(f.command_id);
  } else {
    w.null();
  }
  w.raw(",\"raw\":");
  if (seen) {
    w.string(raw);
  } else {
    w.null();
  }
  w.raw(",\"first\":");
  w.ms_or_null(seen, f.first_seen_s);
  w.raw(",\"last\":");
  w.ms_or_null(seen, f.last_seen_s);
  w.raw(",\"changed\":");
  w.ms_or_null(seen, f.last_changed_s);
  w.raw(",\"changes\":");
  w.number(f.change_count);
  w.raw(",\"status\":");
  w.string(query_status_name(f));
  w.raw(",\"status_at\":");
  w.ms_or_null(queried, f.query_status_at_s);
  w.raw(",\"reg\":");
  w.boolean(registered);

  // The current value first, then the previous ones from the pool; empty without a raw value or without the pool.
  w.raw(",\"hist\":[");
  if (seen && record.history_on) {
    w.raw("[");
    w.ms_or_null(true, f.last_changed_s);
    w.raw(",");
    w.string(raw);
    w.raw("]");
    for (uint8_t i = 0; i < record.history_count; i++) {
      const HistoryValue &h = record.history[i];
      w.raw(",[");
      w.ms_or_null(true, h.first_seen_s);
      w.raw(",");
      w.string(std::string_view(h.bytes, h.len));
      w.raw("]");
    }
  }
  w.raw("]}");
}

/// The sniffer's scan fields, as the page's handler reads them.
struct ScanFields {
  /// A scan was started since boot.
  bool ran;
  bool active;
  /// The last ended run covered all of its IDs. Each run's end sets it.
  bool completed;
  /// The running or last run.
  ScanRun run;
  /// The first ID of `run` not queued yet, NO_SCAN_ID once the last one is. During the wait before a query it names
  /// that query's ID; the sniffer moves past an ID only when it queues the query, so while the last ID's query is in
  /// flight a running scan is at NO_SCAN_ID.
  int current;
};

/// The scan as the page sees it: the `scan` object of the contract.
struct ScanState {
  bool active;
  ScanGoal goal;
  /// False makes `from` and `to` null: the run is a check, or no scan ran yet.
  bool has_range;
  uint8_t from;
  uint8_t to;
  /// The ID the scan queries next; the last ID's while its query is in flight; while idle, the ID the last run
  /// stopped at. NO_SCAN_ID (null) before any run and after a completed one.
  int next;
  /// Idle after a run that covered all of its IDs.
  bool done;
};

/// The scan state from the sniffer's scan fields.
inline ScanState scan_state(const ScanFields &fields) {
  if (!fields.ran) {
    return ScanState{false, DEFAULT_DISCOVER_RUN.goal, false, 0, 0, NO_SCAN_ID, false};
  }
  const bool done = !fields.active && fields.completed;
  int next = NO_SCAN_ID;
  if (!done) {
    next = fields.current == NO_SCAN_ID ? last_scan_id(fields.run) : fields.current;
  }
  return ScanState{
      .active = fields.active,
      .goal = fields.run.goal,
      .has_range = fields.run.goal == ScanGoal::SCAN_GOAL_DISCOVER,
      .from = fields.run.from,
      .to = fields.run.to,
      .next = next,
      .done = done,
  };
}

/// The scan object a start or stop replies with: the state loop() reaches once it applies the request. A start while
/// idle begins `requested` at its first ID; a start while scanning leaves the running scan as it is.
inline ScanState scan_reply(ScanFields fields, bool start, const ScanRun &requested) {
  if (start && !fields.active) {
    return scan_state(ScanFields{
        .ran = true, .active = true, .completed = false, .run = requested, .current = first_scan_id(requested)});
  }
  if (!start) {
    fields.active = false;
  }
  return scan_state(fields);
}

/// Writes the scan object.
inline void write_scan_json(JsonChunkWriter &w, const ScanState &scan) {
  w.raw("{\"active\":");
  w.boolean(scan.active);
  w.raw(",\"goal\":");
  w.string(scan_goal_name(scan.goal));
  w.raw(",\"from\":");
  if (scan.has_range) {
    w.number(scan.from);
  } else {
    w.null();
  }
  w.raw(",\"to\":");
  if (scan.has_range) {
    w.number(scan.to);
  } else {
    w.null();
  }
  w.raw(",\"next\":");
  if (scan.next != NO_SCAN_ID) {
    w.number(static_cast<uint64_t>(scan.next));
  } else {
    w.null();
  }
  w.raw(",\"done\":");
  w.boolean(scan.done);
  w.raw("}");
}

/// The parts of a data response that don't come from the store.
struct DeviceInfo {
  std::string_view mac;
  std::string_view name;
  std::string_view boot;
  uint64_t uptime_ms;
};

/// Writes the whole /sniffer/data response for `since` and flushes it. Runs `with_lock(f)` around every store read:
/// once for the header's `seq` and `dropped`, then once per record to copy it into `scratch`. The JSON of a record
/// is written after its lock is released.
template<typename WithLock, typename IsRegistered>
bool write_data_json(JsonChunkWriter &w, const PropertyStore &store, const DeviceInfo &device, const ScanState &scan,
                     uint32_t since, RecordSnapshot &scratch, WithLock with_lock, IsRegistered is_registered) {
  uint32_t seq = 0;
  uint32_t dropped = 0;
  with_lock([&]() {
    seq = store.seq();
    dropped = store.dropped();
  });

  w.raw("{\"mac\":");
  w.string(device.mac);
  w.raw(",\"name\":");
  w.string(device.name);
  w.raw(",\"boot\":");
  w.string(device.boot);
  w.raw(",\"uptime\":");
  w.number(device.uptime_ms);
  w.raw(",\"seq\":");
  w.number(seq);
  w.raw(",\"full\":");
  w.boolean(since == 0);
  w.raw(",\"dropped\":");
  w.number(dropped);
  w.raw(",\"scan\":");
  write_scan_json(w, scan);
  w.raw(",\"records\":[");

  bool first = true;
  int id = 0;
  while (w.ok()) {
    with_lock([&]() {
      id = next_record_in_delta(store, id, since);
      if (id != DELTA_END) {
        snapshot_record(store, id, scratch);
      }
    });
    if (id == DELTA_END) {
      break;
    }
    if (!first) {
      w.raw(",");
    }
    first = false;
    write_record_json(w, scratch, is_registered(id));
    id++;
  }
  w.raw("]}");
  return w.flush();
}

/// The `since` query parameter of a request URI, 0 (a full response) when missing or not a valid number.
inline uint32_t since_from_uri(std::string_view uri) {
  const auto digits = query_value(uri, "since");
  if (!digits.has_value()) {
    return 0;
  }
  return static_cast<uint32_t>(parse_bounded_uint(*digits, UINT32_MAX).value_or(0));
}

enum class SnifferMethod : uint8_t { METHOD_GET, METHOD_POST, METHOD_OTHER };

enum class SnifferRoute : uint8_t { ROUTE_NONE, ROUTE_PAGE, ROUTE_DATA, ROUTE_SCAN_START, ROUTE_SCAN_STOP };

constexpr std::string_view SNIFFER_PAGE_PATH = "/sniffer";
constexpr std::string_view SNIFFER_DATA_PATH = "/sniffer/data";
constexpr std::string_view SNIFFER_SCAN_START_PATH = "/sniffer/scan/start";
constexpr std::string_view SNIFFER_SCAN_STOP_PATH = "/sniffer/scan/stop";

/// Which sniffer endpoint a request is for. `path` is the URL without its query string.
inline SnifferRoute sniffer_route(SnifferMethod method, std::string_view path) {
  if (method == SnifferMethod::METHOD_GET) {
    if (path == SNIFFER_PAGE_PATH) {
      return SnifferRoute::ROUTE_PAGE;
    }
    if (path == SNIFFER_DATA_PATH) {
      return SnifferRoute::ROUTE_DATA;
    }
    return SnifferRoute::ROUTE_NONE;
  }
  if (method == SnifferMethod::METHOD_POST) {
    if (path == SNIFFER_SCAN_START_PATH) {
      return SnifferRoute::ROUTE_SCAN_START;
    }
    if (path == SNIFFER_SCAN_STOP_PATH) {
      return SnifferRoute::ROUTE_SCAN_STOP;
    }
  }
  return SnifferRoute::ROUTE_NONE;
}

}  // namespace sec_touch
}  // namespace esphome
