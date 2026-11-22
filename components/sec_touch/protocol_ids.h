#pragma once

// Plain C++, no ESPHome includes: the command and property ids of the SEC-Touch protocol and the label and summer
// ventilation meaning tables, shared by _definitions.h, the host-built tests in tests/cpp and the sniffer page. The
// level and preset names live in fan_level.h, beside the level constants they are keyed by.
// ../sec_touch_sniffer/page_bundle.py parses this header at every build: keep its declarations in the forms that
// file's docstring lists.

#include <array>
#include <cstddef>

#include "fan_level.h"

namespace esphome {
namespace sec_touch {

constexpr const int COMMANDID_SET = 32;
constexpr const int COMMANDID_GET = 32800;
constexpr const std::array<int, 6> FAN_LEVEL_IDS = {173, 174, 175, 176, 177, 178};
constexpr const std::array<int, 6> FAN_LABEL_IDS = {78, 79, 80, 81, 82, 83};

constexpr const int SUMMER_VENTILATION_PROPERTY_ID = 48;
constexpr const int DATE_PROPERTY_ID = 1;   // "DD.MM.YY"
constexpr const int CLOCK_PROPERTY_ID = 2;  // "HH:MM:SS"
constexpr const std::array<int, 3> GLOBAL_SETTING_IDS = {SUMMER_VENTILATION_PROPERTY_ID, DATE_PROPERTY_ID,
                                                         CLOCK_PROPERTY_ID};

// Values of the summer ventilation property. The panel sends and expects these raw strings; the hub's atoi parse turns
// them into 0 / 800 for int listeners, and writing "0" / "800" back is silently ignored by the device.
constexpr const char *SUMMER_VENTILATION_ON = "0A00";
constexpr const char *SUMMER_VENTILATION_OFF = "0800";

/// A raw property value and the name the sniffer shows for it.
struct RawValueName {
  const char *raw;
  const char *name;
};

constexpr std::array<RawValueName, 2> SUMMER_VENTILATION_NAMES = {{
    {SUMMER_VENTILATION_ON, "on"},
    {SUMMER_VENTILATION_OFF, "off"},
}};

// Names a fan's label property selects, by value.
constexpr const int NAME_MAPPING_COUNT = 69;
constexpr const char *NAME_MAPPING[NAME_MAPPING_COUNT] = {
    "-",                // 0 do NOT use an empty string since the text sensor will crash :)
    "Bereich 1",        // 1
    "Bereich 2",        // 2
    "Bereich 3",        // 3
    "Bereich 4",        // 4
    "Bereich 5",        // 5
    "Bereich 6",        // 6
    "Wohnzimmer",       // 7
    "Wohnzimmer 1",     // 8
    "Wohnzimmer 2",     // 9
    "Esszimmer",        // 10
    "Esszimmer 1",      // 11
    "Esszimmer 2",      // 12
    "Schlafzimmer",     // 13
    "Schlafzimmer 1",   // 14
    "Schlafzimmer 2",   // 15
    "Kinderzimmer",     // 16
    "Kinderzimmer 1",   // 17
    "Kinderzimmer 2",   // 18
    "Kinderzimmer 3",   // 19
    "Kinderzimmer 4",   // 20
    "Küche",            // 21
    "Küche 1",          // 22
    "Küche 2",          // 23
    "Bad",              // 24
    "Master Bad",       // 25
    "Gäste Bad",        // 26
    "WC",               // 27
    "Gäste WC",         // 28
    "Arbeitszimmer",    // 29
    "Arbeitszimmer 1",  // 30
    "Arbeitszimmer 2",  // 31
    "Hobbyraum",        // 32
    "Mehrzweckraum",    // 33
    "Abstellraum",      // 34
    "Kellerraum",       // 35
    "Kellerraum 1",     // 36
    "Kellerraum 2",     // 37
    "Kellerraum 3",     // 38
    "Dachboden",        // 39
    "Dachboden 1",      // 40
    "Dachboden 2",      // 41
    "Dachboden 3",      // 42
    "Büro",             // 43
    "Büro 1",           // 44
    "Büro 2",           // 45
    "Büro 3",           // 46
    "Büro 4",           // 47
    "Büro 6",           // 48
    "Chef Büro",        // 49
    "Abtl.Ltr. Büro",   // 50
    "Büro EK",          // 51
    "Büro AB",          // 52
    "Büro Entw.",       // 53
    "Büro Konstr.",     // 54
    "Büro Buchh.",      // 55
    "Speiseraum",       // 56
    "Besp. Raum",       // 57
    "Besp. Raum 1",     // 58
    "Besp. Raum 2",     // 59
    "Besp. Raum 3",     // 60
    "Louge",            // 61
    "Bibliothek",       // 62
    "Fitnessraum",      // 63
    "Wintergarten",     // 64
    "Bastelraum",       // 65
    "Ankleidez.",       // 66
    "HWR",              // 67
    "leer"              // 68
};

/// The NAME_MAPPING name a fan's label property selects with `value`, or nullptr when `value` is outside NAME_MAPPING.
constexpr const char *label_name(int value) {
  if (value < 0 || value >= NAME_MAPPING_COUNT) {
    return nullptr;
  }
  return NAME_MAPPING[value];
}

/// Whether `arr` holds `value`.
template<typename T, std::size_t N> constexpr bool contains(const std::array<T, N> &arr, int value) {
  for (const T &item : arr) {
    if (item == value) {
      return true;
    }
  }
  return false;
}

}  // namespace sec_touch
}  // namespace esphome
