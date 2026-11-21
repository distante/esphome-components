#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_json_chunk_writer.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace esphome {
namespace sec_touch {

/// Bytes of JSON buffered before each send.
constexpr size_t JSON_CHUNK_BYTES = 256;

/// Receives each full chunk, and the rest at flush(). Returns false when the bytes could not be sent.
using JsonChunkSink = bool (*)(void *context, const char *data, size_t len);

/// Writes JSON into a fixed buffer and hands it to a sink in chunks of at most JSON_CHUNK_BYTES, so a response of any
/// length needs no heap. Strings are escaped byte-exact: `"` and `\` get their standard escapes, and every byte below
/// 0x20 or from 0x80 up becomes `\u00XX`, one code point per byte (Latin-1). Once the sink fails, every later write
/// is dropped and ok() stays false.
class JsonChunkWriter {
 public:
  JsonChunkWriter(JsonChunkSink sink, void *context) : sink_(sink), context_(context) {}

  /// Starts a new response on `context`: drops what is buffered and clears a failed send.
  void begin(void *context) {
    this->context_ = context;
    this->len_ = 0;
    this->ok_ = true;
  }

  /// Appends JSON text as is: punctuation, keys, literals.
  void raw(std::string_view text) {
    for (char c : text) {
      this->put_(c);
    }
  }

  /// Appends a quoted, escaped string.
  void string(std::string_view value) {
    this->put_('"');
    for (char c : value) {
      this->escaped_(static_cast<uint8_t>(c));
    }
    this->put_('"');
  }

  void number(uint64_t value) {
    // UINT64_MAX has 20 digits.
    char digits[20];
    size_t n = 0;
    do {
      digits[n++] = static_cast<char>('0' + value % 10);
      value /= 10;
    } while (value > 0);
    while (n > 0) {
      this->put_(digits[--n]);
    }
  }

  void boolean(bool value) { this->raw(value ? "true" : "false"); }
  void null() { this->raw("null"); }

  /// Uptime seconds as milliseconds, or `null` when unset.
  void ms_or_null(bool is_set, uint32_t seconds) {
    if (!is_set) {
      this->null();
      return;
    }
    this->number(static_cast<uint64_t>(seconds) * MS_PER_S);
  }

  /// Sends what is buffered.
  bool flush() {
    if (this->len_ == 0 || !this->ok_) {
      return this->ok_;
    }
    this->ok_ = this->sink_(this->context_, this->buf_, this->len_);
    this->len_ = 0;
    return this->ok_;
  }

  bool ok() const { return this->ok_; }

 protected:
  static constexpr uint64_t MS_PER_S = 1000;

  void put_(char c) {
    if (this->len_ == JSON_CHUNK_BYTES) {
      this->flush();
    }
    if (!this->ok_) {
      return;
    }
    this->buf_[this->len_++] = c;
  }

  void escaped_(uint8_t b) {
    if (b == '"' || b == '\\') {
      this->put_('\\');
      this->put_(static_cast<char>(b));
      return;
    }
    if (b >= 0x20 && b < 0x80) {
      this->put_(static_cast<char>(b));
      return;
    }
    static constexpr char HEX[] = "0123456789abcdef";
    this->raw("\\u00");
    this->put_(HEX[b >> 4]);
    this->put_(HEX[b & 0x0F]);
  }

  JsonChunkSink sink_;
  void *context_;
  char buf_[JSON_CHUNK_BYTES];
  size_t len_{0};
  bool ok_{true};
};

}  // namespace sec_touch
}  // namespace esphome
