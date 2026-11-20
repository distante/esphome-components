#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_history_pool.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

namespace esphome {
namespace sec_touch {

/// Property IDs the sniffer records: one slot per ID 0..255, so an ID fits a uint8_t.
constexpr size_t PROPERTY_SLOTS = 256;
/// The highest property ID with a slot. Messages for higher IDs are dropped.
constexpr int MAX_PROPERTY_ID = PROPERTY_SLOTS - 1;

/// Bytes in the history pool, shared by every property ID.
constexpr size_t HISTORY_POOL_BYTES = 2048;

/// Values kept per ID, the current one included. The current value lives in the property record, so the pool keeps
/// one fewer.
constexpr uint8_t HISTORY_DEPTH = 8;

/// Previous raw values of every property, oldest first, in one fixed block of variable-length entries
/// `{id, len, t, bytes}`. Each insert or eviction costs one memmove of at most HISTORY_POOL_BYTES.
class HistoryPool {
 public:
  /// `id` (1 B) + `len` (1 B) + `t` (4 B), before the raw bytes.
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

  /// Adds a previous value of `id`, first seen at `t`. Drops that ID's oldest beyond MAX_PREVIOUS_PER_ID, and while
  /// the pool is full, the oldest entry of the ID with the most entries.
  void push(uint8_t id, uint32_t t, const char *bytes, uint8_t len) {
    const size_t size = ENTRY_HEADER_BYTES + len;
    if (this->buf_ == nullptr || size > HISTORY_POOL_BYTES) {
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
    entry[ENTRY_LEN_OFFSET] = len;
    std::memcpy(entry + ENTRY_TIME_OFFSET, &t, sizeof(t));
    std::memcpy(entry + ENTRY_HEADER_BYTES, bytes, len);
    this->used_ += size;
    this->counts_[id]++;
  }

  /// Calls `f(uint32_t t, const char *bytes, uint8_t len)` for each previous value of `id`, newest first.
  template<typename F> void for_each_newest_first(uint8_t id, F f) const {
    if (this->buf_ == nullptr) {
      return;
    }
    size_t offsets[MAX_PREVIOUS_PER_ID];
    uint8_t found = 0;
    for (size_t off = 0; off < this->used_ && found < MAX_PREVIOUS_PER_ID; off += this->entry_size_(off)) {
      if (this->buf_[off + ENTRY_ID_OFFSET] == id) {
        offsets[found++] = off;
      }
    }
    while (found > 0) {
      const uint8_t *entry = this->buf_.get() + offsets[--found];
      uint32_t t;
      std::memcpy(&t, entry + ENTRY_TIME_OFFSET, sizeof(t));
      f(t, reinterpret_cast<const char *>(entry + ENTRY_HEADER_BYTES), entry[ENTRY_LEN_OFFSET]);
    }
  }

 protected:
  size_t entry_size_(size_t off) const { return ENTRY_HEADER_BYTES + this->buf_[off + ENTRY_LEN_OFFSET]; }

  /// Offset of the first (oldest) entry of `id`. Only called when `id` has entries.
  size_t oldest_of_(uint8_t id) const {
    size_t off = 0;
    while (this->buf_[off + ENTRY_ID_OFFSET] != id) {
      off += this->entry_size_(off);
    }
    return off;
  }

  /// Offset of the oldest entry of the ID with the most entries. Among equally busy IDs, the one with the oldest entry.
  size_t oldest_of_busiest_() const {
    uint8_t most = 0;
    for (uint8_t c : this->counts_) {
      if (c > most) {
        most = c;
      }
    }
    size_t off = 0;
    while (this->counts_[this->buf_[off + ENTRY_ID_OFFSET]] != most) {
      off += this->entry_size_(off);
    }
    return off;
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
