#pragma once

// Plain C++, no ESPHome includes: reads and writes the raw values of a switch time's time and level and of a pair's
// enabled mask (.specs/time-programs.md, "Writing"). Reading reuses the meaning rules of known_property_meaning.h, the
// one definition of each field's valid values. Built on the host by tests/cpp/test_time_program_codec.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "fan_level.h"
#include "known_property_meaning.h"
#include "protocol_ids.h"
#include "time_units.h"

namespace esphome {
namespace sec_touch {

/// A switch time's time of day, as the panel holds it.
struct SwitchTimeTime {
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
};

/// Minutes since midnight: what a written time is compared by, since seconds are never written.
constexpr int minute_of_day(int hour, int minute) { return hour * MINUTES_PER_HOUR + minute; }

/// A time's raw value, read by the time meaning rule, or nullopt when the rule marks it unexpected.
constexpr std::optional<SwitchTimeTime> parse_switch_time_time(std::string_view raw) {
  const std::string_view trimmed = known_property_detail::trim_ascii(raw);
  if (!known_property_detail::is_switch_time_time(trimmed)) {
    return std::nullopt;
  }
  return SwitchTimeTime{static_cast<uint8_t>(known_property_detail::two_digits(trimmed, 0)),
                        static_cast<uint8_t>(known_property_detail::two_digits(trimmed, 3)),
                        static_cast<uint8_t>(known_property_detail::two_digits(trimmed, 6))};
}

/// `HH:MM:00` and its NUL.
constexpr std::size_t SWITCH_TIME_TIME_BUFFER_SIZE = known_property_detail::SWITCH_TIME_TIME_LENGTH + 1;
using SwitchTimeTimeText = std::array<char, SWITCH_TIME_TIME_BUFFER_SIZE>;
/// `HH:MM`
constexpr std::size_t HH_MM_LENGTH = 5;

/// `hh_mm` (`HH:MM`, not checked) as a time is written: `HH:MM:00`, the panel's seconds always 00.
constexpr SwitchTimeTimeText with_written_seconds(std::string_view hh_mm) {
  SwitchTimeTimeText text{};
  for (std::size_t i = 0; i < HH_MM_LENGTH && i < hh_mm.size(); i++) {
    text[i] = hh_mm[i];
  }
  text[HH_MM_LENGTH] = ':';
  text[HH_MM_LENGTH + 1] = '0';
  text[HH_MM_LENGTH + 2] = '0';
  return text;
}

/// Whether `hour` and `minute` name a time of day, as the time meaning rule reads it.
constexpr bool is_time_of_day(int hour, int minute) {
  return hour >= 0 && hour <= known_property_detail::MAX_HOUR && minute >= 0 &&
         minute <= known_property_detail::MAX_MINUTE;
}

/// The raw value a time is written as: `HH:MM:00`. Seconds are dropped, never rounded. Only for a time of day
/// (is_time_of_day).
constexpr SwitchTimeTimeText format_switch_time_time(uint8_t hour, uint8_t minute) {
  const char hh_mm[HH_MM_LENGTH] = {static_cast<char>('0' + hour / 10), static_cast<char>('0' + hour % 10), ':',
                                    static_cast<char>('0' + minute / 10), static_cast<char>('0' + minute % 10)};
  return with_written_seconds(std::string_view(hh_mm, HH_MM_LENGTH));
}

/// A level's raw value read by the level meaning rule (OFF_LEVEL..MAX_SWITCH_TIME_LEVEL), or -1 when the rule marks
/// it unexpected.
constexpr int parse_switch_time_level(std::string_view raw) {
  const std::string_view trimmed = known_property_detail::trim_ascii(raw);
  if (known_property_detail::switch_time_level_name(trimmed) == nullptr) {
    return -1;
  }
  return known_property_detail::whole_decimal(trimmed);
}

/// A level as a decimal and its NUL: the levels a switch time takes have one digit.
constexpr std::size_t SWITCH_TIME_LEVEL_BUFFER_SIZE = 2;
using SwitchTimeLevelText = std::array<char, SWITCH_TIME_LEVEL_BUFFER_SIZE>;
static_assert(MAX_SWITCH_TIME_LEVEL <= 9, "format_switch_time_level() writes one digit");

/// The raw value a level is written as: its decimal.
constexpr SwitchTimeLevelText format_switch_time_level(int level) {
  return SwitchTimeLevelText{static_cast<char>('0' + level), '\0'};
}

/// An enabled mask's raw value read by the enabled meaning rule, or -1 when the rule marks it unexpected.
constexpr int parse_enabled_mask(std::string_view raw) {
  return known_property_detail::enabled_mask(known_property_detail::trim_ascii(raw));
}

/// 2 hex digits and their NUL.
constexpr std::size_t ENABLED_MASK_BUFFER_SIZE = 3;
using EnabledMaskText = std::array<char, ENABLED_MASK_BUFFER_SIZE>;

/// The raw value a mask is written as: 2 uppercase hex digits, like the panel sends.
constexpr EnabledMaskText format_enabled_mask(uint8_t mask) {
  constexpr const char *HEX_DIGITS = "0123456789ABCDEF";
  return EnabledMaskText{HEX_DIGITS[mask >> 4], HEX_DIGITS[mask & 0x0F], '\0'};
}

/// How many options a switch time's level select has: one per level OFF_LEVEL..MAX_SWITCH_TIME_LEVEL.
constexpr std::size_t SWITCH_TIME_LEVEL_OPTION_COUNT = MAX_SWITCH_TIME_LEVEL - OFF_LEVEL + 1;

constexpr std::array<const char *, SWITCH_TIME_LEVEL_OPTION_COUNT> switch_time_level_options() {
  std::array<const char *, SWITCH_TIME_LEVEL_OPTION_COUNT> options{};
  for (std::size_t i = 0; i < SWITCH_TIME_LEVEL_OPTION_COUNT; i++) {
    options[i] = known_property_detail::level_name(OFF_LEVEL + static_cast<int>(i));
  }
  return options;
}

/// The level select's options, by index: index = level, each the level's LEVEL_NAMES name.
inline constexpr std::array<const char *, SWITCH_TIME_LEVEL_OPTION_COUNT> SWITCH_TIME_LEVEL_OPTIONS =
    switch_time_level_options();

static_assert(OFF_LEVEL == 0, "the level select's option index is the level");

}  // namespace sec_touch
}  // namespace esphome
