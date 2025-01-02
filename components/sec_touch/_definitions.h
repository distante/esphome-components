#pragma once

#include <map>
#include <memory>
#include <string>
#include <cstring>
#include "esphome/core/log.h"

namespace esphome {
namespace sec_touch {

#define STX 0x02
#define ETX 0x0A
#define ACK 0x06
#define TAB 0x09

constexpr int COMMANDID_SET = 32;
constexpr int COMMANDID_GET = 32800;
constexpr std::array<int, 6> FAN_LEVEL_IDS = {173, 174, 175, 176, 177, 178};
constexpr std::array<int, 6> FAN_LABEL_IDS = {78, 79, 80, 81, 82, 83};

// Helper function to check if an ID is in the array
template<typename T, size_t N> static bool contains(const std::array<T, N> &arr, int value) {
  return std::find(arr.begin(), arr.end(), value) != arr.end();
}

/**
 * Used to couple `SetDataTask->id` with the levelId or labelId for example
 */
enum class TaskTargetType { LEVEL, LABEL };

enum class TaskState { TO_BE_SENT, SENT_WAITING_ACK };

struct SetDataTask {
  TaskTargetType type;
  int id;
  char value[16];
  TaskState state = TaskState::TO_BE_SENT;

  static std::unique_ptr<SetDataTask> create(TaskTargetType type, int id, const char *value) {
    if ((type == TaskTargetType::LEVEL && contains(FAN_LEVEL_IDS, id)) ||
        (type == TaskTargetType::LABEL && contains(FAN_LABEL_IDS, id))) {
      return std::unique_ptr<SetDataTask>(new SetDataTask(type, id, value));  // Valid task
    }
    return nullptr;  // Null unique_ptr if validation fails
  }

 private:
  SetDataTask(TaskTargetType type, int id, const char *value) : type(type), id(id) {
    std::strncpy(this->value, value, sizeof(this->value) - 1);
    this->value[sizeof(this->value) - 1] = '\0';  // Ensure null termination
  }
};

struct GetDataTask {
  TaskTargetType type;
  int id;
  TaskState state = TaskState::TO_BE_SENT;

  static std::unique_ptr<GetDataTask> create(TaskTargetType type, int id) {
    if ((type == TaskTargetType::LEVEL && contains(FAN_LEVEL_IDS, id)) ||
        (type == TaskTargetType::LABEL && contains(FAN_LABEL_IDS, id))) {
      return std::unique_ptr<GetDataTask>(new GetDataTask(type, id));  // Valid task
    }
    return nullptr;  // Null unique_ptr if validation fails
  }

 private:
  GetDataTask(TaskTargetType type, int id) : type(type), id(id) {}
};

}  // namespace sec_touch
}  // namespace esphome