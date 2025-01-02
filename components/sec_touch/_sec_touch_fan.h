#pragma once

#include "_definitions.h"
#include <map>
#include <string>
#include <cstring>
#include "esphome/core/log.h"

namespace esphome {
namespace sec_touch {

// Class representing a SecTouchFan
class SecTouchFan {
  static constexpr const char *TAG = "SecTouchFan";

 protected:
  int levelId;
  int labelId;
  char fanLevelValue[16] = {'\0'};    // Start empty
  char labelLevelValue[16] = {'\0'};  // Start empty

 public:
  // Constructor
  SecTouchFan(int levelId, int labelId) : levelId(levelId), labelId(labelId) {}

  // Getters
  int get_level_id() const { return levelId; }
  int get_label_id() const { return labelId; }
  const char *get_fan_level_value() const { return fanLevelValue; }
  const char *get_label_level_value() const { return labelLevelValue; }

  // Setters
  void set_fan_level_value(const char *newValue) {
    strncpy(fanLevelValue, newValue, 15);
    fanLevelValue[15] = '\0';  // Ensure null-termination
  }

  void set_label_level_value(const char *newValue) {
    strncpy(labelLevelValue, newValue, 15);
    labelLevelValue[15] = '\0';  // Ensure null-termination
  }

  // Print method for debugging
  void print() const {
    ESP_LOGD(TAG, "Level ID: %d, Label ID: %d, Fan Level Value: %s, Label Level Value: %s", levelId, labelId,
             fanLevelValue, labelLevelValue);
  }
};

// Container for fans with lookup by levelId and labelId
class SecTouchFanManager {
 private:
  static constexpr const char *TAG = "SecTouchFan";
  std::map<int, SecTouchFan *> levelIdMap;
  std::map<int, SecTouchFan *> labelIdMap;

 public:
  void add_fan(SecTouchFan *fan) {
    levelIdMap[fan->get_level_id()] = fan;
    labelIdMap[fan->get_label_id()] = fan;
    ESP_LOGD(TAG, "Added fan with level ID %d and label ID %d", fan->get_level_id(), fan->get_label_id());
  }

  void update_fan_level_value(int levelId, const char *newValue) {
    if (levelIdMap.find(levelId) != levelIdMap.end()) {
      levelIdMap[levelId]->set_fan_level_value(newValue);
    } else {
      ESP_LOGE(TAG, "Level ID %d not found.", levelId);
    }
  }

  void update_label_level_value(int labelId, const char *newValue) {
    if (labelIdMap.find(labelId) != labelIdMap.end()) {
      labelIdMap[labelId]->set_label_level_value(newValue);
    } else {
      ESP_LOGE(TAG, "Label ID %d not found.", labelId);
    }
  }

  // Debug: Print all fans
  void print_all_fans() const {
    if (levelIdMap.empty()) {
      ESP_LOGD(TAG, "No fans available.");
      return;
    }

    for (auto it = levelIdMap.begin(); it != levelIdMap.end(); ++it) {
      // const auto& key = it->first;  // Access the key
      const auto &fan = it->second;  // Access the value
      fan->print();
    }
  }
};

}  // namespace sec_touch
}  // namespace esphome