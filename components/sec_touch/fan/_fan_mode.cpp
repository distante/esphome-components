
#include "_fan_mode.h"
#include "vector"

// Define the global static FanModeMap that is shared across all fans
static const std::unordered_map<FanModeEnum::FanMode, FanModeEnum::FanModeData> FanModeMap = {
    {FanModeEnum::FanMode::NORMAL, {"Normal", 0, 6}},
    {FanModeEnum::FanMode::BURST, {"Burst", 7, esphome::nullopt}},
    {FanModeEnum::FanMode::AUTOMATIC_HUMIDITY, {"Automatic Humidity", 8, esphome::nullopt}},
    {FanModeEnum::FanMode::AUTOMATIC_CO2, {"Automatic CO2", 9, esphome::nullopt}},
    {FanModeEnum::FanMode::AUTOMATIC_TIME, {"Automatic Time", 10, esphome::nullopt}},
    {FanModeEnum::FanMode::SLEEP, {"Sleep", 11, esphome::nullopt}},
};

// Static function to get a reference to the FanModeMap
const std::unordered_map<FanModeEnum::FanMode, FanModeEnum::FanModeData> &FanModeEnum::getFanModeMap() {
  return FanModeMap;
}

// Convert FanMode to string
std::string_view FanModeEnum::to_string(FanMode mode) {
  auto it = getFanModeMap().find(mode);
  return (it != getFanModeMap().end()) ? it->second.str : "Unknown-Error-Mode";
}

// Convert string to FanMode
esphome::optional<FanModeEnum::FanMode> FanModeEnum::from_string(std::string_view str) {
  for (const auto &pair : getFanModeMap()) {
    if (pair.second.str == str) {
      return pair.first;  // Return the FanMode (key)
    }
  }
  return esphome::nullopt;
}

// Get a list of string values dynamically from the map
std::vector<std::string_view> FanModeEnum::getStringValues() {
  std::vector<std::string_view> values;
  for (const auto &pair : getFanModeMap()) {
    values.push_back(pair.second.str);
  }
  return values;
}

FanModeEnum::FanMode FanModeEnum::get_fan_mode_fromSpeed(int speed) {
  // Iterate through FanModeMap to find the correct FanMode based on speed
  for (const auto &pair : getFanModeMap()) {
    const FanModeData &data = pair.second;

    // Check if speed is within the range
    if (speed >= data.start_speed && (!data.end_speed.has_value() || speed <= data.end_speed.value())) {
      return pair.first;  // Return the FanMode (key)
    }
  }

  // Return an invalid mode if no matching FanMode is found
  return FanMode::NORMAL;  // Default or error mode
}

int FanModeEnum::get_start_speed(FanMode mode) { return getFanModeMap().at(mode).start_speed; }