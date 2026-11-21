#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_history_pool.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>

#include "property_slots.h"

namespace esphome {
namespace sec_touch {

/// Bytes in the history pool, shared by every property ID.
constexpr size_t HISTORY_POOL_BYTES = 2048;

/// Values kept per ID, the current one included. The current value lives in the property record, so the pool keeps
/// one fewer.
constexpr uint8_t HISTORY_DEPTH = 8;

/// Previous raw values of every property, oldest first, in one fixed block of variable-length entries
/// `{id, len, first_seen_s, bytes}`. Each insert or eviction costs one memmove of at most HISTORY_POOL_BYTES.
class HistoryPool {
 public:
  /// `id` (1 B) + `len` (1 B) + `first_seen_s` (4 B), before the raw bytes.
  static constexpr size_t ENTRY_HEADER_BYTES = 6;
  static constexpr size_t ENTRY_ID_OFFSET = 0;
  static constexpr size_t ENTRY_LEN_OFFSET = 1;
  static constexpr size_t ENTRY_TIME_OFFSET = 2;
  static constexpr uint8_t MAX_PREVIOUS_PER_ID = HISTORY_DEPTH - 1;

  /// Allocates the pool once. Until then the pool keeps nothing.
  bool allocate() {
    if (this->buf_ != nullptr) {
      return true;
    }
    this->buf_.reset(new (std::nothrow) uint8_t[HISTORY_POOL_BYTES]);
    return this->buf_ != nullptr;
  }

  bool is_allocated() const { return this->buf_ != nullptr; }

  size_t free_bytes() const { return this->buf_ == nullptr ? 0 : HISTORY_POOL_BYTES - this->used_; }

  uint8_t count(uint8_t id) const { return this->counts_[id]; }

  /// Adds a previous value of `id`, first seen at uptime `first_seen_s`. Drops that ID's oldest beyond
  /// MAX_PREVIOUS_PER_ID, and while the pool is full, the oldest entry of the ID with the most entries. A value longer
  /// than an entry's byte-sized length is not kept.
  void push(uint8_t id, uint32_t first_seen_s, std::string_view raw) {
    const size_t size = ENTRY_HEADER_BYTES + raw.size();
    if (this->buf_ == nullptr || raw.size() > UINT8_MAX || size > HISTORY_POOL_BYTES) {
      return;
    }
    if (this->counts_[id] >= MAX_PREVIOUS_PER_ID) {
      this->remove_entry_(this->oldest_of_(id));
    }
    while (this->used_ + size > HISTORY_POOL_BYTES) {
      this->remove_entry_(this->oldest_of_busiest_());
    }

    uint8_t *entry = this->buf_.get() + this->used_;
    entry[ENTRY_ID_OFFSET] = id;
    entry[ENTRY_LEN_OFFSET] = static_cast<uint8_t>(raw.size());
    std::memcpy(entry + ENTRY_TIME_OFFSET, &first_seen_s, sizeof(first_seen_s));
    std::memcpy(entry + ENTRY_HEADER_BYTES, raw.data(), raw.size());
    this->used_ += size;
    this->counts_[id]++;
  }

  /// Calls `f(uint32_t first_seen_s, std::string_view raw)` for each previous value of `id`, newest first.
  template<typename F> void for_each_newest_first(uint8_t id, F f) const {
    if (this->buf_ == nullptr) {
      return;
    }
    const auto is_id = [this, id](size_t off) { return this->buf_[off + ENTRY_ID_OFFSET] == id; };
    size_t offsets[MAX_PREVIOUS_PER_ID];
    uint8_t found = 0;
    for (size_t off = this->find_entry_(0, is_id); off < this->used_ && found < MAX_PREVIOUS_PER_ID;
         off = this->find_entry_(off + this->entry_size_(off), is_id)) {
      offsets[found++] = off;
    }
    while (found > 0) {
      const uint8_t *entry = this->buf_.get() + offsets[--found];
      uint32_t first_seen_s;
      std::memcpy(&first_seen_s, entry + ENTRY_TIME_OFFSET, sizeof(first_seen_s));
      f(first_seen_s,
        std::string_view(reinterpret_cast<const char *>(entry + ENTRY_HEADER_BYTES), entry[ENTRY_LEN_OFFSET]));
    }
  }

 protected:
  size_t entry_size_(size_t off) const { return ENTRY_HEADER_BYTES + this->buf_[off + ENTRY_LEN_OFFSET]; }

  /// Offset of the first entry at or after `from` for which `matches(offset)` holds, oldest first; `used_` if none.
  /// The one place that walks the entries.
  template<typename Pred> size_t find_entry_(size_t from, Pred matches) const {
    size_t off = from;
    while (off < this->used_ && !matches(off)) {
      off += this->entry_size_(off);
    }
    return off;
  }

  /// Offset of the first (oldest) entry of `id`. Only called when `id` has entries.
  size_t oldest_of_(uint8_t id) const {
    return this->find_entry_(0, [this, id](size_t off) { return this->buf_[off + ENTRY_ID_OFFSET] == id; });
  }

  /// Offset of the oldest entry of the ID with the most entries. Among equally busy IDs, the one with the oldest entry.
  size_t oldest_of_busiest_() const {
    uint8_t most = 0;
    for (uint8_t c : this->counts_) {
      if (c > most) {
        most = c;
      }
    }
    return this->find_entry_(
        0, [this, most](size_t off) { return this->counts_[this->buf_[off + ENTRY_ID_OFFSET]] == most; });
  }

  void remove_entry_(size_t off) {
    const size_t size = this->entry_size_(off);
    this->counts_[this->buf_[off + ENTRY_ID_OFFSET]]--;
    std::memmove(this->buf_.get() + off, this->buf_.get() + off + size, this->used_ - off - size);
    this->used_ -= size;
  }

  std::unique_ptr<uint8_t[]> buf_;
  size_t used_{0};
  uint8_t counts_[PROPERTY_SLOTS]{};
};

}  // namespace sec_touch
}  // namespace esphome
