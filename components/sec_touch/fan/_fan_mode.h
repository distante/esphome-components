#pragma once

#include <vector>
#include <string_view>
#include <utility>
#include "esphome/core/optional.h"

class FanModeEnum {
 public:
  // Enum type for Fan Modes
  enum class FanMode { NORMAL, BURST, AUTOMATIC_HUMIDITY, AUTOMATIC_CO2, AUTOMATIC_TIME, SLEEP };

  struct FanModeData {
    std::string_view str;
    int start_speed;
    esphome::optional<int> end_speed;
  };

  // Static function to get the FanMode list (preserves order)
  static const std::vector<std::pair<FanMode, FanModeData>> &getFanModeList();

  // Convert FanMode to string
  static std::string_view to_string(FanMode mode);

  // Convert string to FanMode
  static esphome::optional<FanMode> from_string(std::string_view str);

  // Get a list of string values dynamically from the list
  static std::vector<std::string_view> getStringValues();

  static FanMode get_fan_mode_fromSpeed(int speed);

  static int get_start_speed(FanMode mode);
};
