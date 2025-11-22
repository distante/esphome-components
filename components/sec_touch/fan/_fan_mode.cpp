
#include "_fan_mode.h"
#include "vector"

// Define the global static FanModeList that preserves the intended order
static const std::vector<std::pair<FanModeEnum::FanMode, FanModeEnum::FanModeData>> FanModeList = {
    {FanModeEnum::FanMode::NORMAL, {"Normal", 0, 6}},
    {FanModeEnum::FanMode::BURST, {"Burst", 7, esphome::nullopt}},
    {FanModeEnum::FanMode::AUTOMATIC_HUMIDITY, {"Automatic Humidity", 8, esphome::nullopt}},
    {FanModeEnum::FanMode::AUTOMATIC_CO2, {"Automatic CO2", 9, esphome::nullopt}},
    {FanModeEnum::FanMode::AUTOMATIC_TIME, {"Automatic Time", 10, esphome::nullopt}},
    {FanModeEnum::FanMode::SLEEP, {"Sleep", 11, esphome::nullopt}},
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