#pragma once

// Plain C++, no ESPHome includes: whether a property is known and what its raw value means
// (.specs/sniffer.md, "Known-property catalogue"). No allocation: every text is a static name or a view into `raw`.

#include <optional>
#include <string_view>

#include "protocol_ids.h"

namespace esphome {
namespace sec_touch {

enum class MeaningKind {
  /// A name from the meaning tables in protocol_ids.h.
  NAME,
  /// Date and clock: the trimmed value is its own meaning.
  AS_SENT,
  /// A known property whose value matches no table entry; the page shows "unexpected value <text>".
  UNEXPECTED,
};

struct KnownMeaning {
  MeaningKind kind;
  /// A static name for NAME; for AS_SENT and UNEXPECTED the trimmed raw value, a view into the caller's `raw`.
  std::string_view text;
};

/// Whether `id` is a known property: a fan level or label, summer ventilation, date or clock.
constexpr bool is_known_property(int id) {
  return contains(FAN_LEVEL_IDS, id) || contains(FAN_LABEL_IDS, id) || contains(GLOBAL_SETTING_IDS, id);
}

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

constexpr const char *label_name(int label) {
  if (label < 0 || label >= NAME_MAPPING_COUNT) {
    return nullptr;
  }
  return NAME_MAPPING[label];
}

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

}  // namespace known_property_detail

/// The meaning of a known property's raw value, or nullopt for an unknown property.
inline std::optional<KnownMeaning> known_property_meaning(int id, std::string_view raw) {
  using namespace known_property_detail;
  if (!is_known_property(id)) {
    return std::nullopt;
  }
  const std::string_view trimmed = trim_ascii(raw);
  if (contains(FAN_LEVEL_IDS, id)) {
    const int level = whole_decimal(trimmed);
    return name_or_unexpected(level < 0 ? nullptr : level_name(level), trimmed);
  }
  if (contains(FAN_LABEL_IDS, id)) {
    return name_or_unexpected(label_name(whole_decimal(trimmed)), trimmed);
  }
  if (id == SUMMER_VENTILATION_PROPERTY_ID) {
    return name_or_unexpected(summer_ventilation_name(trimmed), trimmed);
  }
  // Date and clock: the value is its own meaning.
  return KnownMeaning{MeaningKind::AS_SENT, trimmed};
}

}  // namespace sec_touch
}  // namespace esphome
