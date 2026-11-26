#pragma once

// Plain C++, no ESPHome includes: the argument checks and the report of `sec_touch.set_time_program`
// (.specs/time-programs.md, "Bulk-set action"). Built on the host by tests/cpp/test_time_program_bulk.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fan_level.h"
#include "known_property_meaning.h"
#include "protocol_ids.h"
#include "scan_outcome.h"
#include "time_program_codec.h"
#include "time_program_ids.h"
#include "time_program_write.h"

namespace esphome {
namespace sec_touch {

/// The `error` of a call for a pair whose bulk write still runs.
inline constexpr const char *BULK_BUSY = "busy";
/// How the `error` of a call with a bad argument starts.
inline constexpr const char *BULK_INVALID_ARGUMENT = "invalid_argument";
/// Between the ends of a range in an `error`: an en dash.
inline constexpr const char *RANGE_DASH = "–";
/// Between the failed writes in an `error`.
inline constexpr const char *BULK_FAILURE_SEPARATOR = "; ";
/// A bulk write's writes: one per ID of the pair's block, so the 5 times, the 5 levels and the mask.
constexpr std::size_t BULK_WRITE_COUNT = TIME_PROGRAM_IDS_PER_PAIR;

/// A whole program, checked: position i is switch time i + 1.
struct BulkProgram {
  std::array<SwitchTimeTime, SWITCH_TIME_COUNT> times{};
  std::array<int, SWITCH_TIME_COUNT> levels{};
  /// Switch time n's bit (switch_time_bit) is set when it is enabled.
  uint8_t enabled{0};
};

/// A time argument: `HH:MM`, or `HH:MM:SS` whose seconds the write drops. nullopt when it is neither.
inline std::optional<SwitchTimeTime> parse_bulk_time(std::string_view raw) {
  const std::string_view trimmed = known_property_detail::trim_ascii(raw);
  if (trimmed.size() != HH_MM_LENGTH) {
    return parse_switch_time_time(trimmed);
  }
  // HH:MM is read as it would be written, by the time meaning rule, the one parser of a time.
  const SwitchTimeTimeText written = with_written_seconds(trimmed);
  return parse_switch_time_time(std::string_view(written.data(), written.size() - 1));
}

namespace bulk_detail {

inline std::string invalid_argument(const std::string &detail) {
  return std::string(BULK_INVALID_ARGUMENT) + ": " + detail;
}

/// The `error` of a list whose length isn't SWITCH_TIME_COUNT, or nullopt.
inline std::optional<std::string> check_count(const char *name, std::size_t count) {
  if (count == static_cast<std::size_t>(SWITCH_TIME_COUNT)) {
    return std::nullopt;
  }
  return invalid_argument(std::string(name) + " has " + std::to_string(count) + " values, not " +
                          std::to_string(SWITCH_TIME_COUNT));
}

/// "switch time N <field>": how an `error` names a field of a switch time.
inline std::string switch_time_field(int switch_time, TimeProgramField field) {
  return "switch time " + std::to_string(switch_time) + " " + time_program_field_name(field);
}

}  // namespace bulk_detail

/// Checks every argument and fills `out`. Returns the `error` of the first bad one, or nullopt when all are good.
inline std::optional<std::string> read_bulk_program(const std::vector<std::string> &times,
                                                    const std::vector<int32_t> &levels,
                                                    const std::vector<bool> &enabled, BulkProgram &out) {
  if (auto error = bulk_detail::check_count("times", times.size())) {
    return error;
  }
  if (auto error = bulk_detail::check_count("levels", levels.size())) {
    return error;
  }
  if (auto error = bulk_detail::check_count("enabled", enabled.size())) {
    return error;
  }
  BulkProgram program;
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    const std::size_t i = static_cast<std::size_t>(n - 1);
    auto time = parse_bulk_time(times[i]);
    if (!time.has_value()) {
      return bulk_detail::invalid_argument(bulk_detail::switch_time_field(n, TimeProgramField::TIME) + " '" + times[i] +
                                           "' not HH:MM or HH:MM:SS");
    }
    program.times[i] = *time;
  }
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    const std::size_t i = static_cast<std::size_t>(n - 1);
    if (!is_switch_time_level(levels[i])) {
      return bulk_detail::invalid_argument(bulk_detail::switch_time_field(n, TimeProgramField::LEVEL) + " " +
                                           std::to_string(levels[i]) + " not in " + std::to_string(OFF_LEVEL) +
                                           RANGE_DASH + std::to_string(MAX_SWITCH_TIME_LEVEL));
    }
    program.levels[i] = static_cast<int>(levels[i]);
    if (enabled[i]) {
      program.enabled |= switch_time_bit(n);
    }
  }
  out = program;
  return std::nullopt;
}

/**
 * An argument of `sec_touch.set_time_program`, whose own arguments are `Ts`: a constant from the YAML, or a lambda over
 * the call's arguments. Unlike ESPHome's TemplatableValue, it hands out a constant by reference and passes the call's
 * arguments to the lambda by reference, so reading the arguments copies none of the call's lists; only a lambda builds
 * its result.
 */
template<typename T, typename... Ts> class BulkArgument {
 public:
  using Lambda = T (*)(const Ts &...);

  void set(T value) {
    this->value_ = std::move(value);
    this->lambda_ = nullptr;
  }
  void set(Lambda lambda) { this->lambda_ = lambda; }

  /// The value for a call with arguments `x`: the constant, or the lambda's result, kept in `result`.
  const T &get(T &result, const Ts &...x) const {
    if (this->lambda_ == nullptr) {
      return this->value_;
    }
    result = this->lambda_(x...);
    return result;
  }

 protected:
  T value_{};
  Lambda lambda_{nullptr};
};

/// The `error` of a call for a pair that `time_programs` doesn't list.
inline std::string bulk_unlisted_pair_error(int pair) {
  return bulk_detail::invalid_argument("fan_number " + std::to_string(pair) + " not in time_programs");
}

/**
 * Tracks one pair's bulk write, from its start until each of its writes has ended, and builds its report. Each write
 * goes SENDING (its SET queued) -> READ_BACK (the SET ended) -> DONE (its read-back compared, or it failed with nothing
 * more to wait for). The mask write starts READING its fresh GET: until its own SET is queued, a mask SET that ends
 * (an entity write's, already in flight) and its read-back aren't the bulk's. The first failure of a write is the one
 * reported. What each write sent is the entity path's to compare (TimeProgram): a read-back comes here as matched or
 * not.
 */
class BulkWrite {
 public:
  bool active() const { return this->active_; }

  /// Starts tracking the writes of a program.
  void begin() {
    this->active_ = true;
    this->writes_.fill(Write{});
    this->mask_write_().state = State::READING;
  }

  /// The mask SET built from the fresh GET was queued.
  void mask_set_queued() {
    if (!this->active_ || this->mask_write_().state != State::READING) {
      return;
    }
    this->mask_write_().state = State::SENDING;
  }

  /// A SET of `slot` ended; `read_back_follows` when its read-back GET was queued.
  void set_ended(const TimeProgramSlot &slot, bool read_back_follows) {
    Write *write = this->in_state_(slot, State::SENDING);
    if (write == nullptr) {
      return;
    }
    write->state = read_back_follows ? State::READ_BACK : State::DONE;
  }

  /// A delivered read-back of `slot`; `matched` when it equals what was sent.
  void read_back(const TimeProgramSlot &slot, bool matched) {
    Write *write = this->in_state_(slot, State::READ_BACK);
    if (write == nullptr) {
      return;
    }
    if (!matched) {
      record_(*write, WriteFailure::NOT_KEPT);
    }
    write->state = State::DONE;
  }

  /// A GET of `slot` ended; a write still waiting for its read-back gets none. Answered but not compared means a newer
  /// SET of the ID was queued: the bulk value isn't kept.
  void get_ended(const TimeProgramSlot &slot, TaskEnd end) {
    Write *write = this->in_state_(slot, State::READ_BACK);
    if (write == nullptr) {
      return;
    }
    record_(*write, end == TaskEnd::TASK_END_ANSWERED ? WriteFailure::NOT_KEPT : WriteFailure::TIMEOUT);
    write->state = State::DONE;
  }

  /// An entity write replaced the bulk value of `slot` before it was confirmed: it isn't kept. The write still waits
  /// for its SET and read-back.
  void replaced(const TimeProgramSlot &slot) {
    Write *write = this->write_(slot);
    if (write == nullptr || write->state == State::DONE) {
      return;
    }
    record_(*write, WriteFailure::NOT_KEPT);
  }

  /// A write of `slot` failed. MASK_UNREADABLE ends the mask write: nothing was sent.
  void fail(const TimeProgramSlot &slot, WriteFailure failure) {
    Write *write = this->write_(slot);
    if (write == nullptr || write->state == State::DONE) {
      return;
    }
    // Until the bulk's mask SET is queued, a failed mask SET is an earlier write's.
    if (write->state == State::READING && failure != WriteFailure::MASK_UNREADABLE) {
      return;
    }
    record_(*write, failure);
    if (failure == WriteFailure::MASK_UNREADABLE) {
      write->state = State::DONE;
    }
  }

  /// Every write has ended.
  bool done() const {
    if (!this->active_) {
      return false;
    }
    for (const Write &write : this->writes_) {
      if (write.state != State::DONE) {
        return false;
      }
    }
    return true;
  }

  /// Ends tracking. Returns the `error` when a write failed, or nullopt when every read-back matched.
  std::optional<std::string> finish() {
    this->active_ = false;
    std::size_t failed = 0;
    for (const Write &write : this->writes_) {
      if (write.failure.has_value()) {
        failed++;
      }
    }
    if (failed == 0) {
      return std::nullopt;
    }
    std::string error = std::to_string(failed) + " of " + std::to_string(BULK_WRITE_COUNT) + " writes failed: ";
    bool first = true;
    auto add = [this, &error, &first](TimeProgramField field, int switch_time) {
      const Write &write = this->writes_[time_program_block_index(field, switch_time)];
      if (!write.failure.has_value()) {
        return;
      }
      if (!first) {
        error += BULK_FAILURE_SEPARATOR;
      }
      first = false;
      error += field == TimeProgramField::ENABLED ? std::string(time_program_field_name(field))
                                                  : bulk_detail::switch_time_field(switch_time, field);
      error += " ";
      error += write_failure_name(*write.failure);
    };
    // In write order: the times, the levels, then the mask.
    for (TimeProgramField field : {TimeProgramField::TIME, TimeProgramField::LEVEL}) {
      for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
        add(field, n);
      }
    }
    add(TimeProgramField::ENABLED, 0);
    return error;
  }

 protected:
  enum class State : uint8_t { READING, SENDING, READ_BACK, DONE };

  struct Write {
    State state{State::SENDING};
    std::optional<WriteFailure> failure{};
  };

  Write *write_(const TimeProgramSlot &slot) {
    if (!this->active_) {
      return nullptr;
    }
    if (slot.field != TimeProgramField::ENABLED && !is_switch_time(slot.switch_time)) {
      return nullptr;
    }
    return &this->writes_[time_program_block_index(slot)];
  }

  Write &mask_write_() { return this->writes_[time_program_block_index(TimeProgramField::ENABLED, 0)]; }

  Write *in_state_(const TimeProgramSlot &slot, State state) {
    Write *write = this->write_(slot);
    return write != nullptr && write->state == state ? write : nullptr;
  }

  static void record_(Write &write, WriteFailure failure) {
    if (!write.failure.has_value()) {
      write.failure = failure;
    }
  }

  bool active_{false};
  /// By time_program_block_index().
  std::array<Write, BULK_WRITE_COUNT> writes_{};
};

}  // namespace sec_touch
}  // namespace esphome
