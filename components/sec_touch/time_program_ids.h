#pragma once

// Plain C++, no ESPHome includes: a fan pair's time-program IDs, looked up in the arrays of protocol_ids.h
// (.specs/time-programs.md, "Implementation notes"). Built on the host by tests/cpp/test_time_program_ids.

#include <array>
#include <cstddef>
#include <optional>

#include "protocol_ids.h"

namespace esphome {
namespace sec_touch {

/// Fan pairs with a time program, numbered 1 to TIME_PROGRAM_PAIR_COUNT.
constexpr int TIME_PROGRAM_PAIR_COUNT = static_cast<int>(TIME_PROGRAM_ENABLED_IDS.size());
/// One pair's block: its enabled mask, then a time and a level per switch time.
constexpr std::size_t TIME_PROGRAM_IDS_PER_PAIR = 1 + 2 * SWITCH_TIME_COUNT;

/// Which part of a time program a property holds.
enum class TimeProgramField : uint8_t {
  /// The pair's enabled mask; it has no switch time of its own.
  ENABLED,
  TIME,
  LEVEL,
};

/// How the log and the bulk report name a field: "enabled mask", or the "time" or "level" of a switch time.
constexpr const char *time_program_field_name(TimeProgramField field) {
  switch (field) {
    case TimeProgramField::ENABLED:
      return "enabled mask";
    case TimeProgramField::TIME:
      return "time";
    case TimeProgramField::LEVEL:
      return "level";
  }
  return "?";
}

/// Where a time-program ID sits: its pair (1-based), its switch time (1-based, 0 for the enabled mask) and field.
struct TimeProgramSlot {
  int pair;
  int switch_time;
  TimeProgramField field;
};

constexpr bool is_time_program_pair(int pair) { return pair >= 1 && pair <= TIME_PROGRAM_PAIR_COUNT; }
constexpr bool is_switch_time(int switch_time) { return switch_time >= 1 && switch_time <= SWITCH_TIME_COUNT; }

/// The enabled-mask ID of `pair`, or -1 when `pair` has no time program.
constexpr int time_program_enabled_id(int pair) {
  if (!is_time_program_pair(pair)) {
    return -1;
  }
  return TIME_PROGRAM_ENABLED_IDS[pair - 1];
}

/// The time ID of switch time `switch_time` of `pair`, or -1 when either is out of range.
constexpr int time_program_time_id(int pair, int switch_time) {
  if (!is_time_program_pair(pair) || !is_switch_time(switch_time)) {
    return -1;
  }
  return TIME_PROGRAM_TIME_IDS[(pair - 1) * SWITCH_TIME_COUNT + (switch_time - 1)];
}

/// The level ID of switch time `switch_time` of `pair`, or -1 when either is out of range.
constexpr int time_program_level_id(int pair, int switch_time) {
  if (!is_time_program_pair(pair) || !is_switch_time(switch_time)) {
    return -1;
  }
  return TIME_PROGRAM_LEVEL_IDS[(pair - 1) * SWITCH_TIME_COUNT + (switch_time - 1)];
}

/// Where `id` sits in the time programs, or nullopt when it is not a time-program ID.
constexpr std::optional<TimeProgramSlot> time_program_slot(int id) {
  for (int pair = 1; pair <= TIME_PROGRAM_PAIR_COUNT; pair++) {
    if (time_program_enabled_id(pair) == id) {
      return TimeProgramSlot{pair, 0, TimeProgramField::ENABLED};
    }
    for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
      if (time_program_time_id(pair, n) == id) {
        return TimeProgramSlot{pair, n, TimeProgramField::TIME};
      }
      if (time_program_level_id(pair, n) == id) {
        return TimeProgramSlot{pair, n, TimeProgramField::LEVEL};
      }
    }
  }
  return std::nullopt;
}

/// The IDs one poll of a pair queues, each once, in the order they were added. Fixed size: a pair has no more than
/// TIME_PROGRAM_IDS_PER_PAIR IDs.
class TimeProgramPollList {
 public:
  /// Adds `id` unless it is already listed or the list is full. Returns whether `id` is listed afterwards.
  bool add(int id) {
    for (std::size_t i = 0; i < this->count_; i++) {
      if (this->ids_[i] == id) {
        return true;
      }
    }
    if (this->count_ >= this->ids_.size()) {
      return false;
    }
    this->ids_[this->count_++] = id;
    return true;
  }

  std::size_t size() const { return this->count_; }
  const int *begin() const { return this->ids_.data(); }
  const int *end() const { return this->ids_.data() + this->count_; }

 protected:
  std::array<int, TIME_PROGRAM_IDS_PER_PAIR> ids_{};
  std::size_t count_{0};
};

}  // namespace sec_touch
}  // namespace esphome
