#pragma once

// Plain C++, no ESPHome includes: whether a property is known and what its raw value means
// (.specs/sniffer.md, "Known-property catalogue"). No allocation: every text is a static name or a view into `raw`.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "fan_level.h"
#include "protocol_ids.h"
#include "time_units.h"

namespace esphome {
namespace sec_touch {

enum class MeaningKind {
  /// A name from the meaning tables in protocol_ids.h and fan_level.h, or an enabled mask's name.
  NAME,
  /// Date, clock and a switch time's time: the trimmed value is its own meaning.
  AS_SENT,
  /// A known property whose value matches no table entry; the page shows "unexpected value <text>".
  UNEXPECTED,
};

struct KnownMeaning {
  MeaningKind kind;
  /// A static name for NAME; for AS_SENT and UNEXPECTED the trimmed raw value, a view into the caller's `raw`.
  std::string_view text;
};

/// Whether `id` is a known property: a fan level or label, summer ventilation, date, clock, or a time-program ID.
constexpr bool is_known_property(int id) {
  return contains(FAN_LEVEL_IDS, id) || contains(FAN_LABEL_IDS, id) || contains(GLOBAL_SETTING_IDS, id) ||
         contains(TIME_PROGRAM_ENABLED_IDS, id) || contains(TIME_PROGRAM_TIME_IDS, id) ||
         contains(TIME_PROGRAM_LEVEL_IDS, id);
}

/// The highest known property ID: the largest ID of the arrays is_known_property() reads.
constexpr int max_known_property_id() {
  int max = 0;
  for (int id : FAN_LEVEL_IDS) {
    max = id > max ? id : max;
  }
  for (int id : FAN_LABEL_IDS) {
    max = id > max ? id : max;
  }
  for (int id : GLOBAL_SETTING_IDS) {
    max = id > max ? id : max;
  }
  for (int id : TIME_PROGRAM_ENABLED_IDS) {
    max = id > max ? id : max;
  }
  for (int id : TIME_PROGRAM_TIME_IDS) {
    max = id > max ? id : max;
  }
  for (int id : TIME_PROGRAM_LEVEL_IDS) {
    max = id > max ? id : max;
  }
  return max;
}

/// The bit of switch time `switch_time` (1-based) in a pair's enabled mask.
constexpr uint8_t switch_time_bit(int switch_time) { return static_cast<uint8_t>(1 << (switch_time - 1)); }

namespace known_property_detail {

constexpr bool is_ascii_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

constexpr std::string_view trim_ascii(std::string_view raw) {
  while (!raw.empty() && is_ascii_space(raw.front())) {
    raw.remove_prefix(1);
  }
  while (!raw.empty() && is_ascii_space(raw.back())) {
    raw.remove_suffix(1);
  }
  return raw;
}

/// No table entry is above this, so a longer number is never read in full (and can't overflow).
constexpr int MAX_TABLE_VALUE = NOT_CONNECTED_LEVEL;

/// `text` read as a whole decimal (1+ ASCII digits, leading zeros allowed), or -1 when it is not one or is above
/// MAX_TABLE_VALUE.
constexpr int whole_decimal(std::string_view text) {
  if (text.empty()) {
    return -1;
  }
  int value = 0;
  for (char c : text) {
    if (c < '0' || c > '9') {
      return -1;
    }
    value = value * 10 + (c - '0');
    if (value > MAX_TABLE_VALUE) {
      return -1;
    }
  }
  return value;
}

constexpr const char *level_name(int level) {
  for (const LevelName &entry : LEVEL_NAMES) {
    if (entry.level == level) {
      return entry.name;
    }
  }
  return nullptr;
}

constexpr bool no_level_above_max_table_value() {
  for (const LevelName &entry : LEVEL_NAMES) {
    if (entry.level > MAX_TABLE_VALUE) {
      return false;
    }
  }
  return true;
}

// whole_decimal() gives up above MAX_TABLE_VALUE, so no table entry may sit above it.
static_assert(NAME_MAPPING_COUNT - 1 <= MAX_TABLE_VALUE, "a NAME_MAPPING value is above MAX_TABLE_VALUE");
static_assert(no_level_above_max_table_value(), "a LEVEL_NAMES level is above MAX_TABLE_VALUE");

constexpr const char *summer_ventilation_name(std::string_view trimmed) {
  for (const RawValueName &entry : SUMMER_VENTILATION_NAMES) {
    if (trimmed == entry.raw) {
      return entry.name;
    }
  }
  return nullptr;
}

constexpr KnownMeaning name_or_unexpected(const char *name, std::string_view trimmed) {
  if (name == nullptr) {
    return KnownMeaning{MeaningKind::UNEXPECTED, trimmed};
  }
  return KnownMeaning{MeaningKind::NAME, name};
}

/// The name of a switch time's level: a fan level name for OFF_LEVEL..MAX_SWITCH_TIME_LEVEL, else nullptr.
constexpr const char *switch_time_level_name(std::string_view trimmed) {
  const int level = whole_decimal(trimmed);
  if (level < OFF_LEVEL || level > MAX_SWITCH_TIME_LEVEL) {
    return nullptr;
  }
  return level_name(level);
}

/// The highest enabled mask: one bit per switch time, none above.
constexpr int MAX_ENABLED_MASK = (1 << SWITCH_TIME_COUNT) - 1;

/// The value of an ASCII hex digit in either case, or -1 when `c` is not one.
constexpr int hex_digit(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return -1;
}

/// `trimmed` read as an enabled mask (exactly 2 hex digits, at most MAX_ENABLED_MASK), or -1 when it is not one.
constexpr int enabled_mask(std::string_view trimmed) {
  if (trimmed.size() != 2) {
    return -1;
  }
  const int high = hex_digit(trimmed[0]);
  const int low = hex_digit(trimmed[1]);
  if (high < 0 || low < 0) {
    return -1;
  }
  const int mask = high * 16 + low;
  if (mask > MAX_ENABLED_MASK) {
    return -1;
  }
  return mask;
}

/// Room for the longest enabled mask name, every switch time enabled, and its terminating NUL.
constexpr std::size_t ENABLED_NAME_CAPACITY = 24;

/// An enabled mask's name, built at compile time: see ENABLED_MASK_NONE_NAME in protocol_ids.h.
struct EnabledName {
  std::array<char, ENABLED_NAME_CAPACITY> text{};
  std::size_t length = 0;
  /// Set when the name did not fit ENABLED_NAME_CAPACITY; a static_assert below rejects it.
  bool overflow = false;

  constexpr void append(const char *part) {
    for (; *part != 0; part++) {
      if (this->length + 1 >= ENABLED_NAME_CAPACITY) {
        this->overflow = true;
        return;
      }
      this->text[this->length++] = *part;
    }
  }

  constexpr void append_number(int number) {
    // A switch time's number is 1..SWITCH_TIME_COUNT; the static_assert below keeps it one digit.
    const char digit[2] = {static_cast<char>('0' + number), 0};
    this->append(digit);
  }
};

static_assert(SWITCH_TIME_COUNT <= 9, "enabled_name() writes each switch time's number as one digit");

/// The name of enabled mask `mask`: the enabled switch times' numbers, ascending and separated, then the suffix, or
/// the none name when no bit is set.
constexpr EnabledName enabled_name(int mask) {
  EnabledName name;
  if (mask == 0) {
    name.append(ENABLED_MASK_NONE_NAME);
    return name;
  }
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    if ((mask & switch_time_bit(n)) == 0) {
      continue;
    }
    if (name.length > 0) {
      name.append(ENABLED_MASK_SEPARATOR);
    }
    name.append_number(n);
  }
  name.append(ENABLED_MASK_SUFFIX);
  return name;
}

constexpr std::array<EnabledName, MAX_ENABLED_MASK + 1> enabled_names() {
  std::array<EnabledName, MAX_ENABLED_MASK + 1> names{};
  for (int mask = 0; mask <= MAX_ENABLED_MASK; mask++) {
    names[mask] = enabled_name(mask);
  }
  return names;
}

/// Every enabled mask's name, by mask: static, so a meaning can point at its name.
inline constexpr std::array<EnabledName, MAX_ENABLED_MASK + 1> ENABLED_NAMES = enabled_names();

constexpr bool no_enabled_name_overflows() {
  for (const EnabledName &name : ENABLED_NAMES) {
    if (name.overflow) {
      return false;
    }
  }
  return true;
}

static_assert(no_enabled_name_overflows(), "an enabled mask name is longer than ENABLED_NAME_CAPACITY");

constexpr KnownMeaning enabled_mask_meaning(std::string_view trimmed) {
  const int mask = enabled_mask(trimmed);
  if (mask < 0) {
    return KnownMeaning{MeaningKind::UNEXPECTED, trimmed};
  }
  const EnabledName &name = ENABLED_NAMES[mask];
  return KnownMeaning{MeaningKind::NAME, std::string_view(name.text.data(), name.length)};
}

constexpr bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }

/// The two ASCII digits at `text[at]` as a number, or -1 when they are not two digits.
constexpr int two_digits(std::string_view text, std::size_t at) {
  if (!is_ascii_digit(text[at]) || !is_ascii_digit(text[at + 1])) {
    return -1;
  }
  return (text[at] - '0') * 10 + (text[at + 1] - '0');
}

constexpr int MAX_HOUR = HOURS_PER_DAY - 1;
constexpr int MAX_MINUTE = MINUTES_PER_HOUR - 1;
constexpr int MAX_SECOND = SECONDS_PER_MINUTE - 1;
/// `HH:MM:SS`
constexpr std::size_t SWITCH_TIME_TIME_LENGTH = 8;

/// Whether `trimmed` is exactly `HH:MM:SS`, HH 00-MAX_HOUR, MM 00-MAX_MINUTE and SS 00-MAX_SECOND.
constexpr bool is_switch_time_time(std::string_view trimmed) {
  if (trimmed.size() != SWITCH_TIME_TIME_LENGTH || trimmed[2] != ':' || trimmed[5] != ':') {
    return false;
  }
  const int hour = two_digits(trimmed, 0);
  const int minute = two_digits(trimmed, 3);
  const int second = two_digits(trimmed, 6);
  return hour >= 0 && hour <= MAX_HOUR && minute >= 0 && minute <= MAX_MINUTE && second >= 0 && second <= MAX_SECOND;
}

}  // namespace known_property_detail

// known_property_meaning() names a branch for each global setting; a new one needs its meaning decided there.
static_assert(GLOBAL_SETTING_IDS.size() == 3,
              "a global setting ID was added: decide its meaning in known_property_meaning()");

/// The meaning of a known property's raw value, or nullopt for an unknown property.
inline std::optional<KnownMeaning> known_property_meaning(int id, std::string_view raw) {
  if (!is_known_property(id)) {
    return std::nullopt;
  }
  const std::string_view trimmed = known_property_detail::trim_ascii(raw);
  if (contains(FAN_LEVEL_IDS, id)) {
    return known_property_detail::name_or_unexpected(
        known_property_detail::level_name(known_property_detail::whole_decimal(trimmed)), trimmed);
  }
  if (contains(FAN_LABEL_IDS, id)) {
    return known_property_detail::name_or_unexpected(label_name(known_property_detail::whole_decimal(trimmed)),
                                                     trimmed);
  }
  if (id == SUMMER_VENTILATION_PROPERTY_ID) {
    return known_property_detail::name_or_unexpected(known_property_detail::summer_ventilation_name(trimmed), trimmed);
  }
  if (id == DATE_PROPERTY_ID || id == CLOCK_PROPERTY_ID) {
    return KnownMeaning{MeaningKind::AS_SENT, trimmed};
  }
  if (contains(TIME_PROGRAM_LEVEL_IDS, id)) {
    return known_property_detail::name_or_unexpected(known_property_detail::switch_time_level_name(trimmed), trimmed);
  }
  if (contains(TIME_PROGRAM_ENABLED_IDS, id)) {
    return known_property_detail::enabled_mask_meaning(trimmed);
  }
  if (contains(TIME_PROGRAM_TIME_IDS, id)) {
    if (!known_property_detail::is_switch_time_time(trimmed)) {
      return KnownMeaning{MeaningKind::UNEXPECTED, trimmed};
    }
    return KnownMeaning{MeaningKind::AS_SENT, trimmed};
  }
  // Unreachable while the static_assert above holds: every known property has a branch.
  return std::nullopt;
}

}  // namespace sec_touch
}  // namespace esphome
