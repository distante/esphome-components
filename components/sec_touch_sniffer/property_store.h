#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_property_store.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>

#include "../sec_touch/data_message.h"
#include "../sec_touch/scan_outcome.h"
#include "history_pool.h"
#include "property_slots.h"

namespace esphome {
namespace sec_touch {

/// Longest raw value kept, never truncated: the longest value the hub's frame buffer can deliver.
constexpr size_t RAW_VALUE_CAP = MAX_DATA_VALUE_LEN;
static_assert(RAW_VALUE_CAP <= UINT8_MAX,
              "PropertyFields::raw_len and HistoryPool entries hold a raw length in a byte");

enum class RecordResult : uint8_t {
  /// First message for the ID.
  RECORD_NEW,
  /// The raw bytes differ from the current raw value.
  RECORD_CHANGED,
  /// Same raw bytes: only `last_seen` (and the command id) moved.
  RECORD_UNCHANGED,
  /// The ID has no slot, so the message was counted in `dropped()` and not stored.
  RECORD_DROPPED,
  /// Not stored: the store is not allocated, or the value is longer than RAW_VALUE_CAP, which the hub's frame buffer
  /// can't deliver.
  RECORD_REJECTED,
};

/// A property record's fixed-size fields. Times are device uptime in seconds.
struct PropertyFields {
  uint32_t first_seen_s;
  uint32_t last_seen_s;
  uint32_t last_changed_s;
  uint32_t query_status_at_s;
  uint32_t change_count;
  /// The store's `seq` at this record's latest update.
  uint32_t seq;
  uint16_t command_id;
  /// Meaningful only with FLAG_QUERIED set; unqueried otherwise.
  ScanOutcome query_status;
  uint8_t raw_len;
  uint8_t flags;
};

/// One property record per ID 0..MAX_PROPERTY_ID, in two blocks allocated once: the fields and the raw values. The
/// history pool is a third block, allocated only on request.
class PropertyStore {
 public:
  static constexpr uint8_t FLAG_SEEN = 1 << 0;
  static constexpr uint8_t FLAG_QUERIED = 1 << 1;

  /// Allocates the record blocks, and the history pool when `with_history`. Until then every message is dropped.
  ///
  /// Heap, despite "avoid dynamic memory allocation": the sniffer is a development tool, flashed to discover
  /// properties rather than kept on every device. Each block is allocated once at setup and never freed, so the heap
  /// does not fragment at runtime; separate blocks stay under 16 KB each, the pool exists only with the page, and a
  /// failed allocation marks the component failed instead of crashing.
  bool allocate(bool with_history) {
    if (this->fields_ == nullptr) {
      this->fields_.reset(new (std::nothrow) PropertyFields[PROPERTY_SLOTS]());
      this->raw_.reset(new (std::nothrow) char[PROPERTY_SLOTS * RAW_VALUE_CAP]);
    }
    if (with_history && !this->history_.allocate()) {
      return false;
    }
    return this->is_allocated();
  }

  bool is_allocated() const { return this->fields_ != nullptr && this->raw_ != nullptr; }

  /// Records a data message. A change is any difference in the raw bytes; a different command id alone is not.
  RecordResult record_message(int command_id, int property_id, std::string_view raw, uint32_t now_s) {
    if (!this->has_slot_(property_id)) {
      this->dropped_++;
      return RecordResult::RECORD_DROPPED;
    }
    // Rejected rather than truncated: raw values are byte-exact.
    if (raw.size() > RAW_VALUE_CAP || !this->is_allocated()) {
      return RecordResult::RECORD_REJECTED;
    }
    PropertyFields &f = this->fields_[property_id];
    char *stored = this->raw_slot_(property_id);
    f.command_id = static_cast<uint16_t>(command_id);
    f.last_seen_s = now_s;
    f.seq = ++this->seq_;

    if ((f.flags & FLAG_SEEN) == 0) {
      f.flags |= FLAG_SEEN;
      f.first_seen_s = now_s;
      f.last_changed_s = now_s;
      this->store_raw_(f, stored, raw);
      return RecordResult::RECORD_NEW;
    }
    if (std::string_view(stored, f.raw_len) == raw) {
      return RecordResult::RECORD_UNCHANGED;
    }

    this->history_.push(static_cast<uint8_t>(property_id), f.last_changed_s, std::string_view(stored, f.raw_len));
    f.last_changed_s = now_s;
    f.change_count++;
    this->store_raw_(f, stored, raw);
    return RecordResult::RECORD_CHANGED;
  }

  /// Records the outcome of the sniffer's own scan query. A failure leaves the raw value, history and change count.
  void record_query(int property_id, ScanOutcome outcome, uint32_t now_s) {
    if (!this->has_slot_(property_id) || !this->is_allocated()) {
      return;
    }
    PropertyFields &f = this->fields_[property_id];
    f.flags |= FLAG_QUERIED;
    f.query_status = outcome;
    f.query_status_at_s = now_s;
    f.seq = ++this->seq_;
  }

  /// Only call with an ID 0..MAX_PROPERTY_ID on an allocated store.
  const PropertyFields &fields(int id) const { return this->fields_[id]; }
  bool has_raw_value(int id) const { return (this->fields_[id].flags & FLAG_SEEN) != 0; }
  /// False while the query status is `unqueried`.
  bool is_queried(int id) const { return (this->fields_[id].flags & FLAG_QUERIED) != 0; }
  std::string_view raw_value(int id) const {
    return std::string_view(this->raw_.get() + raw_offset_(id), this->fields_[id].raw_len);
  }

  /// Goes up on every record update, `last_seen` included.
  uint32_t seq() const { return this->seq_; }
  /// Messages not stored: IDs above MAX_PROPERTY_ID.
  uint32_t dropped() const { return this->dropped_; }
  const HistoryPool &history() const { return this->history_; }

 protected:
  static bool has_slot_(int id) { return id >= 0 && id <= MAX_PROPERTY_ID; }
  static size_t raw_offset_(int id) { return static_cast<size_t>(id) * RAW_VALUE_CAP; }
  char *raw_slot_(int id) { return this->raw_.get() + raw_offset_(id); }

  static void store_raw_(PropertyFields &f, char *stored, std::string_view raw) {
    std::memcpy(stored, raw.data(), raw.size());
    f.raw_len = static_cast<uint8_t>(raw.size());
  }

  std::unique_ptr<PropertyFields[]> fields_;
  std::unique_ptr<char[]> raw_;
  HistoryPool history_;
  uint32_t seq_{0};
  uint32_t dropped_{0};
};

}  // namespace sec_touch
}  // namespace esphome
