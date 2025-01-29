#pragma once

#include <array>
#include <string>
#include <set>
#include <functional>
#include "esphome/core/optional.h"

class FanModeEnum {
 public:
  // Enum type for Fan Modes
  enum class FanMode { NORMAL, BURST, AUTOMATIC_HUMIDITY, AUTOMATIC_CO2, AUTOMATIC_TIME, SLEEP };

  // Static mapping of FanMode enum values to strings
  static constexpr std::array<std::pair<FanMode, std::string_view>, 6> FanModeStrings = {
      {{FanMode::NORMAL, "Normal"},
       {FanMode::BURST, "Burst"},
       {FanMode::AUTOMATIC_HUMIDITY, "Automatic Humidity"},
       {FanMode::AUTOMATIC_CO2, "Automatic CO2"},
       {FanMode::AUTOMATIC_TIME, "Automatic Time"},
       {FanMode::SLEEP, "Sleep"}}};

  // Convert FanMode to string
  static esphome::optional<std::string_view> toString(FanMode mode) {
    for (const auto &pair : FanModeStrings) {
      if (pair.first == mode) {
        return pair.second;
      }
    }
    return esphome::nullopt;  // Return an empty optional if not found
  }

  // Convert string to FanMode
  static esphome::optional<FanMode> fromString(std::string_view str) {
    for (const auto &pair : FanModeStrings) {
      if (pair.second == str) {
        return pair.first;
      }
    }
    return esphome::nullopt;  // Return an empty optional if not found
  }

  // Get a set of string values
  static std::set<std::string> getStringValues() {
    std::set<std::string> values;
    for (const auto &pair : FanModeStrings) {
      values.insert(std::string(pair.second));  // Convert string_view to string and insert into set
    }
    return values;
  }
};
