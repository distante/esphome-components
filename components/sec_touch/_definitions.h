#pragma once

#include <map>
#include <memory>
#include <string>
#include <cstring>
#include "esphome/core/log.h"
#include <functional>

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

/**
 * Callback for when a property is updated
 * @param property_id The ID of the property that was updated for example 173 for level of the fan 1
 * @param new_value The new value of the property obtained from the touch controller
 */
using UpdateCallbackListener = std::function<void(int property_id, int new_value)>;

static std::string replace_special_characters(const char *buffer) {
  std::string result;
  while (*buffer != '\0') {
    switch (*buffer) {
      case STX:  // STX
        result += "[STX]";
        break;
      case ETX:  // ETX
        result += "[ETX]";
        break;
      case TAB:  // ETX
        result += "[TAB]";
        break;
      default:
        result += *buffer;  // Append regular characters as is
    }
    buffer++;
  }
  return result;
}

// Helper function to check if an ID is in the array
template<typename T, size_t N> static bool contains(const std::array<T, N> &arr, int value) {
  return std::find(arr.begin(), arr.end(), value) != arr.end();
}

/**
 * Used to couple `SetDataTask->id` with the level_id or label_id for example
 */
enum class TaskTargetType { LEVEL, LABEL };

enum class TaskState { TO_BE_SENT, WAITING_ACK, WAITING_DATA, TO_BE_PROCESSED };

class EnumToString {
 private:
  EnumToString() = default;

 public:
  static const char *TaskTargetType(TaskTargetType v) {
    switch (v) {
      case TaskTargetType::LEVEL:
        return "LEVEL";
      case TaskTargetType::LABEL:
        return "LABEL";
      default:
        return "UNKNOWN";
    }
  }

  static const char *TaskState(TaskState v) {
    switch (v) {
      case TaskState::TO_BE_SENT:
        return "TO_BE_SENT";
      case TaskState::WAITING_ACK:
        return "WAITING_ACK";
      case TaskState::WAITING_DATA:
        return "WAITING_DATA";
      case TaskState::TO_BE_PROCESSED:
        return "TO_BE_PROCESSED";
      default:
        return "UNKNOWN";
    }
  }
};

struct SetDataTask {
  TaskTargetType targetType;
  int id;
  char value[16];
  TaskState state = TaskState::TO_BE_SENT;

  static std::unique_ptr<SetDataTask> create(TaskTargetType targetType, int id, const char *value) {
    if ((targetType == TaskTargetType::LEVEL && contains(FAN_LEVEL_IDS, id)) ||
        (targetType == TaskTargetType::LABEL && contains(FAN_LABEL_IDS, id))) {
      return std::unique_ptr<SetDataTask>(new SetDataTask(targetType, id, value));  // Valid task
    }
    return nullptr;  // Null unique_ptr if validation fails
  }

 private:
  SetDataTask(TaskTargetType targetType, int id, const char *value) : targetType(targetType), id(id) {
    std::strncpy(this->value, value, sizeof(this->value) - 1);
    this->value[sizeof(this->value) - 1] = '\0';  // Ensure null termination
  }
};

struct IncomingMessage {
  std::string returned_id = "";
  std::string returned_value = "";

  char buffer[64];
  size_t buffer_index = -1;
};

struct GetDataTask {
  TaskTargetType targetType;
  int id;
  TaskState state = TaskState::TO_BE_SENT;

  static std::unique_ptr<GetDataTask> create(TaskTargetType targetType, int id) {
    if ((targetType == TaskTargetType::LEVEL && contains(FAN_LEVEL_IDS, id)) ||
        (targetType == TaskTargetType::LABEL && contains(FAN_LABEL_IDS, id))) {
      return std::unique_ptr<GetDataTask>(new GetDataTask(targetType, id));  // Valid task
    }
    return nullptr;  // Null unique_ptr if validation fails
  }

 private:
  GetDataTask(TaskTargetType targetType, int id) : targetType(targetType), id(id) {}
};

}  // namespace sec_touch
}  // namespace esphome