
#include "_fan_mode.h"
#include "vector"
#include "../fan_level.h"
#include "../protocol_ids.h"

namespace level = esphome::sec_touch;

// Define the global static FanModeList that preserves the intended order
static const std::vector<std::pair<FanModeEnum::FanMode, FanModeEnum::FanModeData>> FanModeList = {
    {FanModeEnum::FanMode::NORMAL, {level::NORMAL_PRESET_NAME, level::NORMAL_PRESET_LEVEL, level::MAX_NORMAL_LEVEL}},
    {FanModeEnum::FanMode::BURST, {level::BURST_NAME, level::BURST_LEVEL, esphome::nullopt}},
    {FanModeEnum::FanMode::AUTOMATIC_HUMIDITY,
     {level::AUTOMATIC_HUMIDITY_NAME, level::AUTOMATIC_HUMIDITY_LEVEL, esphome::nullopt}},
    {FanModeEnum::FanMode::AUTOMATIC_CO2, {level::AUTOMATIC_CO2_NAME, level::AUTOMATIC_CO2_LEVEL, esphome::nullopt}},
    {FanModeEnum::FanMode::AUTOMATIC_TIME, {level::AUTOMATIC_TIME_NAME, level::AUTOMATIC_TIME_LEVEL, esphome::nullopt}},
    {FanModeEnum::FanMode::SLEEP, {level::SLEEP_NAME, level::SLEEP_LEVEL, esphome::nullopt}},
};

// Static function to get a reference to the FanModeList
const std::vector<std::pair<FanModeEnum::FanMode, FanModeEnum::FanModeData>> &FanModeEnum::getFanModeList() {
  return FanModeList;
}

// Convert FanMode to string
std::string_view FanModeEnum::to_string(FanMode mode) {
  for (const auto &pair : getFanModeList()) {
    if (pair.first == mode)
      return pair.second.str;
  }
  return "Unknown-Error-Mode";
}

// Convert string to FanMode
esphome::optional<FanModeEnum::FanMode> FanModeEnum::from_string(std::string_view str) {
  for (const auto &pair : getFanModeList()) {
    if (pair.second.str == str) {
      return pair.first;  // Return the FanMode (key)
    }
  }
  return esphome::nullopt;
}

// Get a list of string values dynamically from the list
std::vector<std::string_view> FanModeEnum::getStringValues() {
  std::vector<std::string_view> values;
  for (const auto &pair : getFanModeList()) {
    values.push_back(pair.second.str);
  }
  return values;
}

FanModeEnum::FanMode FanModeEnum::get_fan_mode_fromSpeed(int speed) {
  // Iterate through FanModeList to find the correct FanMode based on speed
  for (const auto &pair : getFanModeList()) {
    const FanModeData &data = pair.second;

    // Check if speed is within the range
    if (speed >= data.start_speed && (!data.end_speed.has_value() || speed <= data.end_speed.value())) {
      return pair.first;  // Return the FanMode (key)
    }
  }

  // Return default mode if no matching FanMode is found
  return FanMode::NORMAL;  // Default or error mode
}

int FanModeEnum::get_start_speed(FanMode mode) {
  for (const auto &pair : getFanModeList()) {
    if (pair.first == mode)
      return pair.second.start_speed;
  }
  return 0;
}

const std::vector<const char *> &FanModeEnum::getPresetModePointers() {
  static std::vector<const char *> pointers;
  if (pointers.empty()) {
    for (const auto &pair : getFanModeList()) {
      pointers.push_back(pair.second.str.data());
    }
  }
  return pointers;
}
