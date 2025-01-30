#pragma once

#include <unordered_map>
#include <string_view>
#include "esphome/core/optional.h"
#include "vector"

class FanModeEnum {
 public:
  // Enum type for Fan Modes
  enum class FanMode { NORMAL, BURST, AUTOMATIC_HUMIDITY, AUTOMATIC_CO2, AUTOMATIC_TIME, SLEEP };

  struct FanModeData {
    std::string_view str;
    int fromSpeed;
    esphome::optional<int> toSpeed;
  };

  // Static function to get the FanModeMap
  static const std::unordered_map<FanMode, FanModeData> &getFanModeMap();

  // Convert FanMode to string
  static std::string_view toString(FanMode mode);

  // Convert string to FanMode
  static esphome::optional<FanMode> fromString(std::string_view str);

  // Get a list of string values dynamically from the map
  static std::vector<std::string_view> getStringValues();
};
