#pragma once

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
  int getLevelId() const { return levelId; }
  int getLabelId() const { return labelId; }
  const char *getFanLevelValue() const { return fanLevelValue; }
  const char *getLabelLevelValue() const { return labelLevelValue; }

  // Setters
  void setFanLevelValue(const char *newValue) {
    strncpy(fanLevelValue, newValue, 15);
    fanLevelValue[15] = '\0';  // Ensure null-termination
  }

  void setLabelLevelValue(const char *newValue) {
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
  // Add a fan
  void addFan(SecTouchFan *fan) {
    levelIdMap[fan->getLevelId()] = fan;
    labelIdMap[fan->getLabelId()] = fan;
    ESP_LOGD(TAG, "Added fan with level ID %d and label ID %d", fan->getLevelId(), fan->getLabelId());
  }

  // Update fanLevelValue using levelId
  void updateFanLevelValue(int levelId, const char *newValue) {
    if (levelIdMap.find(levelId) != levelIdMap.end()) {
      levelIdMap[levelId]->setFanLevelValue(newValue);
    }
  }

  // Update labelLevelValue using labelId
  void updateLabelLevelValue(int labelId, const char *newValue) {
    if (labelIdMap.find(labelId) != labelIdMap.end()) {
      labelIdMap[labelId]->setLabelLevelValue(newValue);
    }
  }

  // Debug: Print all fans
  void printAllFans() const {
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