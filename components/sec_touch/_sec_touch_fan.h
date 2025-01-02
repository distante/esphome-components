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
  const int level_id;
  const int label_id;
  /*

FAN LEVEL OF 7 means Stosslüften
FAN LEVEL OF 11 means Schlummer
FAN LEVEL OF 10 means Automatik Zeit
FAN LEVEL OF 8 means Automatik Feuchte
FAN LEVEL OF 9 means Automatik CO2

*/
  int level_value = -1;
  int label_value = -1;  // Start empty

 public:
  // Constructor
  SecTouchFan(int level_id, int label_id) : level_id(level_id), label_id(label_id) {}

  // Getters
  int get_level_id() const { return level_id; }
  int get_label_id() const { return label_id; }
  const int *get_level_value() const { return &level_value; }
  const int *get_label_value() const { return &label_value; }

  void set_level_value(int newValue) { level_value = newValue; }

  void set_label_value(int newValue) { label_value = newValue; }

  // Print method for debugging
  void printConfig() const {
    ESP_LOGCONFIG(TAG, "Level ID: %d, Label ID: %d, Fan Level Value: %d, Label Level Value: %d", level_id, label_id,
                  level_value, label_value);
  }
};

// Container for fans with lookup by level_id and label_id
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

  void update_fan_after_task_ended(TaskTargetType targetType, int id, int value) {
    switch (targetType) {
      case TaskTargetType::LEVEL:
        update_fan_level_value(id, value);
        break;
      case TaskTargetType::LABEL:
        update_label_level_value(id, value);
        break;
      default:
        ESP_LOGE(TAG, "Unknown task targetType %s to update id %d", EnumToString::TaskTargetType(targetType), id);

        break;
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
      fan->printConfig();
    }
  }

 protected:
  void update_fan_level_value(int level_id, int newValue) {
    if (levelIdMap.find(level_id) != levelIdMap.end()) {
      levelIdMap[level_id]->set_level_value(newValue);
    } else {
      ESP_LOGE(TAG, "Could not update. Level ID %d not found.", level_id);
    }
  }

  void update_label_level_value(int label_id, int newValue) {
    if (labelIdMap.find(label_id) != labelIdMap.end()) {
      labelIdMap[label_id]->set_label_value(newValue);
    } else {
      ESP_LOGE(TAG, "Could not update. Label ID %d not found.", label_id);
    }
  }
};

}  // namespace sec_touch
}  // namespace esphome