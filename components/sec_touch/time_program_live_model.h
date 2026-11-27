#pragma once

// Plain C++, no ESPHome includes: a fan pair's live state, computed from its time program, its level ID and the panel
// clock (.specs/time-programs.md, "Live state"). Built on the host by tests/cpp/test_time_program_live_model.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "fan_level.h"
#include "known_property_meaning.h"
#include "protocol_ids.h"
#include "time_program_codec.h"
#include "time_program_ids.h"
#include "time_units.h"

namespace esphome {
namespace sec_touch {

/// The state words of the live-state entities. `not running`, not `off`: `off` is level 0's name.
inline constexpr const char *LIVE_STATE_NONE = "none";
inline constexpr const char *LIVE_STATE_NOT_RUNNING = "not running";
inline constexpr const char *LIVE_STATE_CONFLICT = "conflict";
/// A switch time in effect shows as this prefix and its number: `switch time 3`.
inline constexpr const char *SWITCH_TIME_NAME_PREFIX = "switch time ";

namespace live_model_detail {

/// Room for the prefix, one digit and the NUL.
constexpr std::size_t SWITCH_TIME_NAME_CAPACITY = 14;

struct SwitchTimeName {
  std::array<char, SWITCH_TIME_NAME_CAPACITY> text{};
};

static_assert(SWITCH_TIME_COUNT <= 9, "switch_time_name() writes the number as one digit");

constexpr SwitchTimeName make_switch_time_name(int switch_time) {
  SwitchTimeName name;
  std::size_t length = 0;
  for (const char *c = SWITCH_TIME_NAME_PREFIX; *c != 0; c++) {
    name.text[length++] = *c;
  }
  name.text[length] = static_cast<char>('0' + switch_time);
  return name;
}

constexpr std::array<SwitchTimeName, SWITCH_TIME_COUNT> make_switch_time_names() {
  std::array<SwitchTimeName, SWITCH_TIME_COUNT> names{};
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    names[n - 1] = make_switch_time_name(n);
  }
  return names;
}

inline constexpr std::array<SwitchTimeName, SWITCH_TIME_COUNT> SWITCH_TIME_NAMES = make_switch_time_names();

}  // namespace live_model_detail

/// `switch time N` for switch time `switch_time` (1-based): a static text.
constexpr const char *switch_time_name(int switch_time) {
  return live_model_detail::SWITCH_TIME_NAMES[switch_time - 1].text.data();
}

constexpr uint32_t MS_PER_DAY = static_cast<uint32_t>(SECONDS_PER_DAY) * MS_PER_S;

/// How late a clock read can arrive: the panel drops the milliseconds, then the reply travels (about 1 s); 2 s leaves a
/// margin.
constexpr uint32_t LATE_CLOCK_READ_MS = 2 * MS_PER_S;

/// Milliseconds forward from `from` to `to` on a 24 h dial; 0 when equal.
constexpr uint32_t ms_forward(uint32_t from, uint32_t to) { return (to + MS_PER_DAY - from) % MS_PER_DAY; }

/// What the live-state entities show at one moment.
struct LiveState {
  /// `switch time N` or a state word.
  const char *in_effect;
  /// A level name or a state word.
  const char *running_level;
  /// A level name or a state word.
  const char *next_level;
  /// Milliseconds from now to the next change, or nullopt when there is none.
  std::optional<uint32_t> ms_to_next_change;
};

/// The panel clock, read from ID 2 and advanced with the caller's millisecond clock between reads.
class PanelClock {
 public:
  /// Takes a raw clock value read at `now_ms`. Returns false, and keeps the old anchor, for a value that isn't
  /// `HH:MM:SS`.
  bool take(std::string_view raw, uint32_t now_ms) {
    // The clock has the same HH:MM:SS form as a switch time's time.
    auto time = parse_switch_time_time(raw);
    if (!time.has_value()) {
      return false;
    }
    this->anchor_ms_of_day_ = ms_of_day_of(*time);
    this->anchor_ms_ = now_ms;
    this->set_ = true;
    return true;
  }
  bool is_set() const { return this->set_; }
  /// Milliseconds since the panel's midnight at `now_ms`.
  uint32_t ms_of_day(uint32_t now_ms) const {
    // Unsigned: right across the millis() wrap.
    const uint32_t since_anchor = now_ms - this->anchor_ms_;
    return (this->anchor_ms_of_day_ + since_anchor % MS_PER_DAY) % MS_PER_DAY;
  }

  /// Milliseconds since midnight of a panel time of day.
  static constexpr uint32_t ms_of_day_of(const SwitchTimeTime &time) {
    return (static_cast<uint32_t>(minute_of_day(time.hour, time.minute)) * SECONDS_PER_MINUTE + time.second) * MS_PER_S;
  }

 protected:
  uint32_t anchor_ms_of_day_{0};
  uint32_t anchor_ms_{0};
  bool set_{false};
};

/// A pair's live-state inputs, each its last good value, and the state they give.
class LiveStateModel {
 public:
  explicit LiveStateModel(int pair) : pair_(pair) {}

  /// Takes a raw value of one of the pair's time-program IDs. Returns false, and keeps the input's last good value,
  /// for a value the meaning rule marks unexpected.
  /// A slot of another pair is no input: it returns false too.
  bool take_block_value(TimeProgramSlot slot, std::string_view raw) {
    if (slot.pair != this->pair_) {
      return false;
    }
    switch (slot.field) {
      case TimeProgramField::ENABLED:
        return take_if_good(this->mask_, parse_enabled_mask(raw));
      case TimeProgramField::TIME: {
        auto time = parse_switch_time_time(raw);
        if (!time.has_value()) {
          return false;
        }
        this->times_ms_[slot.switch_time - 1] = static_cast<int32_t>(PanelClock::ms_of_day_of(*time));
        return true;
      }
      case TimeProgramField::LEVEL:
        return take_if_good(this->levels_[slot.switch_time - 1], parse_switch_time_level(raw));
    }
    return false;
  }
  /// Takes a raw value of the pair's level ID, read by the fan level meaning rule. Returns false, and keeps the last
  /// good value, for an unexpected one.
  bool take_pair_level(std::string_view raw) {
    const int level = known_property_detail::whole_decimal(known_property_detail::trim_ascii(raw));
    if (known_property_detail::level_name(level) == nullptr) {
      return false;
    }
    this->pair_level_ = level;
    return true;
  }
  /// Takes a raw clock value read at `now_ms`. Returns false, and keeps the old anchor, for a value that isn't
  /// `HH:MM:SS`. A late read across a change is accepted but keeps the old anchor.
  bool take_clock(std::string_view raw, uint32_t now_ms) {
    PanelClock read;
    if (!read.take(raw, now_ms)) {
      return false;
    }
    if (!this->is_late_read_across_a_change(read, now_ms)) {
      this->clock_ = read;
    }
    return true;
  }

  /// The state at `now_ms`, or nullopt while an input the state needs was never read well: the pair level always;
  /// while running, also the clock, the mask, and each enabled switch time's time and level.
  std::optional<LiveState> compute(uint32_t now_ms) const {
    if (this->pair_level_ == NEVER_READ) {
      return std::nullopt;
    }
    if (this->pair_level_ != AUTOMATIC_TIME_LEVEL) {
      return LiveState{LIVE_STATE_NOT_RUNNING, LIVE_STATE_NOT_RUNNING, LIVE_STATE_NOT_RUNNING, std::nullopt};
    }
    if (!this->has_running_inputs()) {
      return std::nullopt;
    }
    const int32_t now = static_cast<int32_t>(this->clock_.ms_of_day(now_ms));
    // The latest enabled time at or before now, and the latest of all (yesterday's last, when none is before now).
    int32_t latest_before = NEVER_READ;
    int32_t latest = NEVER_READ;
    // The earliest enabled time strictly after now, and the earliest of all (tomorrow's first, when none is after).
    int32_t earliest_after = NEVER_READ;
    int32_t earliest = NEVER_READ;
    for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
      if (!this->is_enabled(n)) {
        continue;
      }
      const int32_t t = this->times_ms_[n - 1];
      latest = std::max(latest, t);
      earliest = earliest == NEVER_READ ? t : std::min(earliest, t);
      if (t <= now) {
        latest_before = std::max(latest_before, t);
      } else {
        earliest_after = earliest_after == NEVER_READ ? t : std::min(earliest_after, t);
      }
    }
    if (latest == NEVER_READ) {
      return LiveState{LIVE_STATE_NONE, LIVE_STATE_NONE, LIVE_STATE_NONE, std::nullopt};
    }
    const int32_t in_effect_at = latest_before != NEVER_READ ? latest_before : latest;
    const int32_t next_at = earliest_after != NEVER_READ ? earliest_after : earliest;
    const int in_effect = this->only_switch_time_at(in_effect_at);
    const int next = this->only_switch_time_at(next_at);
    // Not ms_forward: a next change at now is the only one left, so its next turn is a full day away, not 0.
    const uint32_t wait = next_at > now ? static_cast<uint32_t>(next_at - now)
                                        : static_cast<uint32_t>(next_at) + MS_PER_DAY - static_cast<uint32_t>(now);
    return LiveState{in_effect > 0 ? switch_time_name(in_effect) : LIVE_STATE_CONFLICT, this->level_name_of(in_effect),
                     this->level_name_of(next), wait};
  }

 protected:
  static constexpr int32_t NEVER_READ = -1;

  static constexpr std::array<int32_t, SWITCH_TIME_COUNT> never_read_all() {
    std::array<int32_t, SWITCH_TIME_COUNT> inputs{};
    for (int32_t &input : inputs) {
      input = NEVER_READ;
    }
    return inputs;
  }

  static bool take_if_good(int32_t &input, int value) {
    if (value < 0) {
      return false;
    }
    input = value;
    return true;
  }

  /// Whether the inputs a running pair's state needs were each read well once. A disabled switch time's are not.
  bool has_running_inputs() const {
    if (this->mask_ == NEVER_READ || !this->clock_.is_set()) {
      return false;
    }
    for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
      if (!this->is_enabled(n)) {
        continue;
      }
      if (this->times_ms_[n - 1] == NEVER_READ || this->levels_[n - 1] == NEVER_READ) {
        return false;
      }
    }
    return true;
  }

  bool is_enabled(int switch_time) const { return (this->mask_ & switch_time_bit(switch_time)) != 0; }

  bool is_late_read_across_a_change(const PanelClock &read, uint32_t now_ms) const {
    if (!this->clock_.is_set() || this->mask_ == NEVER_READ) {
      return false;
    }
    const uint32_t from = read.ms_of_day(now_ms);
    const uint32_t back = ms_forward(from, this->clock_.ms_of_day(now_ms));
    if (back == 0 || back >= LATE_CLOCK_READ_MS) {
      return false;
    }
    for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
      const int32_t t = this->times_ms_[n - 1];
      if (!this->is_enabled(n) || t == NEVER_READ) {
        continue;
      }
      // The switch time lies in (from, the old clock]: the old clock has passed it, the read has not.
      const uint32_t after_from = ms_forward(from, static_cast<uint32_t>(t));
      if (after_from > 0 && after_from <= back) {
        return true;
      }
    }
    return false;
  }

  /// The one enabled switch time at `at`, or 0 when two or more are tied there.
  int only_switch_time_at(int32_t at) const {
    int found = 0;
    for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
      if (!this->is_enabled(n) || this->times_ms_[n - 1] != at) {
        continue;
      }
      if (found != 0) {
        return 0;
      }
      found = n;
    }
    return found;
  }

  /// The level name of switch time `switch_time`, or the conflict word for 0.
  const char *level_name_of(int switch_time) const {
    if (switch_time == 0) {
      return LIVE_STATE_CONFLICT;
    }
    return known_property_detail::level_name(this->levels_[switch_time - 1]);
  }

  int pair_;
  int32_t mask_{NEVER_READ};
  /// Per switch time: its time in milliseconds since midnight, and its level.
  std::array<int32_t, SWITCH_TIME_COUNT> times_ms_{never_read_all()};
  std::array<int32_t, SWITCH_TIME_COUNT> levels_{never_read_all()};
  int32_t pair_level_{NEVER_READ};
  PanelClock clock_;
};

/// The `next_change` timestamp: the time source's `now_s` plus `ms_to_next_change`, rounded to the minute.
constexpr int64_t next_change_timestamp(int64_t now_s, uint32_t ms_to_next_change) {
  const int64_t at_ms = now_s * MS_PER_S + ms_to_next_change;
  return (at_ms + MS_PER_MINUTE / 2) / MS_PER_MINUTE * SECONDS_PER_MINUTE;
}

}  // namespace sec_touch
}  // namespace esphome
