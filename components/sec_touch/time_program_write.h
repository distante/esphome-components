#pragma once

// Plain C++, no ESPHome includes: the types of a pair's `write_failed` event (.specs/time-programs.md, "Writing").
// Built on the host by tests/cpp/test_time_program_codec.

#include <array>
#include <cstdint>
#include <optional>

#include "scan_outcome.h"

namespace esphome {
namespace sec_touch {

/// Why a time-program write failed. The order matches WRITE_FAILURE_NAMES.
enum class WriteFailure : uint8_t {
  /// The SET got a NAK.
  REFUSED,
  /// The SET got no answer, or was dropped from the queue before it was sent.
  TIMEOUT,
  /// The read-back differs from what was sent.
  NOT_KEPT,
  /// The fresh mask GET failed; nothing was written.
  MASK_UNREADABLE,
};

/// The `write_failed` event types, by WriteFailure: the event entity's types and the names the log shows.
inline constexpr std::array<const char *, 4> WRITE_FAILURE_NAMES = {"refused", "timeout", "not_kept",
                                                                    "mask_unreadable"};

constexpr const char *write_failure_name(WriteFailure failure) {
  return WRITE_FAILURE_NAMES[static_cast<uint8_t>(failure)];
}

/// How a SET that ended with `end` failed, or nullopt when it did not. A reply that couldn't be parsed is no failure:
/// the read-back decides whether the write landed.
constexpr std::optional<WriteFailure> set_write_failure(TaskEnd end) {
  switch (end) {
    case TaskEnd::TASK_END_NAK:
      return WriteFailure::REFUSED;
    case TaskEnd::TASK_END_TIMEOUT:
      return WriteFailure::TIMEOUT;
    case TaskEnd::TASK_END_ANSWERED:
    case TaskEnd::TASK_END_MALFORMED:
      return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace sec_touch
}  // namespace esphome
