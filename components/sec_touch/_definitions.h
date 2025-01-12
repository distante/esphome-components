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
#define NAK 0x15
#define TAB 0x09
#define NOISE 0xFF  // 255

constexpr const int COMMANDID_SET = 32;
constexpr const int COMMANDID_GET = 32800;
constexpr const std::array<int, 6> FAN_LEVEL_IDS = {173, 174, 175, 176, 177, 178};
constexpr const std::array<int, 6> FAN_LABEL_IDS = {78, 79, 80, 81, 82, 83};
constexpr const int NAME_MAPPING_COUNT = 70;
constexpr const char *NAME_MAPPING[NAME_MAPPING_COUNT] = {
    "",                 // 0
    "Bereich 1",        // 1
    "Bereich 2",        // 2
    "Bereich 3",        // 3
    "Bereich 4",        // 4
    "Bereich 5",        // 5
    "Bereich 6",        // 6
    "Wohnzimmer",       // 7
    "Wohnzimmer 1",     // 8
    "Wohnzimmer 2",     // 9
    "Esszimmer",        // 10
    "Esszimmer 1",      // 11
    "Esszimmer 2",      // 12
    "Schlafzimmer",     // 13
    "Schlafzimmer 1",   // 14
    "Schlafzimmer 2",   // 15
    "Kinderzimmer",     // 16
    "Kinderzimmer 1",   // 17
    "Kinderzimmer 2",   // 18
    "Kinderzimmer 3",   // 19
    "Kinderzimmer 4",   // 20
    "Küche",            // 21
    "Küche 1",          // 22
    "Küche 2",          // 23
    "Bad",              // 24
    "Master Bad",       // 25
    "Gäste Bad",        // 26
    "WC",               // 27
    "Gäste WC",         // 28
    "Arbeitszimmer",    // 29
    "Arbeitszimmer 1",  // 30
    "Arbeitszimmer 2",  // 31
    "Hobbyraum",        // 32
    "Mehrzweckraum",    // 33
    "Abstellraum",      // 34
    "Kellerraum",       // 35
    "Kellerraum 1",     // 36
    "Kellerraum 2",     // 37
    "Kellerraum 3",     // 38
    "Dachboden",        // 39
    "Dachboden 1",      // 40
    "Dachboden 2",      // 41
    "Dachboden 3",      // 42
    "Büro",             // 43
    "Büro 1",           // 44
    "Büro 2",           // 45
    "Büro 3",           // 46
    "Büro 4",           // 47
    "Büro 6",           // 48
    "Chef Büro",        // 49
    "Abtl.Ltr. Büro",   // 50
    "Büro EK",          // 51
    "Büro AB",          // 52
    "Büro Entw.",       // 53
    "Büro Konstr.",     // 54
    "Büro Buchh.",      // 55
    "Speiseraum",       // 56
    "Besp. Raum",       // 57
    "Besp. Raum 1",     // 58
    "Besp. Raum 2",     // 59
    "Besp. Raum 3",     // 60
    "Louge",            // 61
    "Bibliothek",       // 62
    "Fitnessraum",      // 63
    "Wintergarten",     // 64
    "Bastelraum",       // 65
    "Ankleidez.",       // 66
    "HWR",              // 67
    "leer"              // 68
};

/**
 * Callback for when a property is updated
 * @param property_id The ID of the property that was updated for example 173 for level of the fan 1
 * @param new_value The new value of the property obtained from the touch controller
 */
using UpdateCallbackListener = std::function<void(int property_id, int new_value)>;

// Helper function to check if an ID is in the array
template<typename T, size_t N> static bool contains(const std::array<T, N> &arr, int value) {
  return std::find(arr.begin(), arr.end(), value) != arr.end();
}

/**
 * Used to couple `SetDataTask->id` with the level_id or label_id for example
 */
enum class TaskTargetType { LEVEL, LABEL };
enum class TaskType {
  SET,
  GET,
  /**
   * Used to indicate that no task is currently being processed
   */
  NONE
};

enum class TaskState { TO_BE_SENT, TO_BE_PROCESSED };

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

      case TaskState::TO_BE_PROCESSED:
        return "TO_BE_PROCESSED";
      default:
        return "UNKNOWN";
    }
  }
};

struct IncomingMessage {
 private:
  static constexpr const char *EMPTY = "---1";
  std::string returned_id = IncomingMessage::EMPTY;
  std::string returned_value = IncomingMessage::EMPTY;

 public:
  char buffer[64];
  size_t buffer_index = -1;

  void reset() {
    this->buffer_index = -1;
    this->returned_id = IncomingMessage::EMPTY;
    this->returned_value = IncomingMessage::EMPTY;
    memset(this->buffer, 0, sizeof(this->buffer));
  }

  bool returned_id_is_empty() { return this->returned_id == IncomingMessage::EMPTY; }
  bool returned_value_is_empty() { return this->returned_value == IncomingMessage::EMPTY; }

  void add_to_returned_id(char data) {
    if (this->returned_id_is_empty()) {
      this->returned_id = "";
    }
    this->returned_id += data;
  }

  void add_to_returned_value(char data) {
    if (this->returned_value_is_empty()) {
      this->returned_value = "";
    }
    this->returned_value += data;
  }

  std::string get_returned_id() { return this->returned_id; }
  std::string get_returned_value() { return this->returned_value; }

  int get_returned_id_as_int() { return std::stoi(this->returned_id); }
  int get_returned_value_as_int() { return std::stoi(this->returned_value); }

  /**
   * @returns the index of the last byte stored in the buffer
   */
  int store_data(uint8_t data) {
    if (this->buffer_index + 1 < sizeof(this->buffer) - 1) {
      this->buffer_index++;
      this->buffer[this->buffer_index] = data;

    } else {
      ESP_LOGW("sec-touch:incoming", "  Buffer overflow! Discarding extra data.");
    }

    return this->buffer_index;
  }
};

struct SetDataTask {
  TaskTargetType targetType;
  int property_id;
  char value[8];
  TaskState state = TaskState::TO_BE_SENT;

  const TaskType taskType = TaskType::SET;

  static std::unique_ptr<SetDataTask> create(TaskTargetType targetType, int property_id, const char *value) {
    if ((targetType == TaskTargetType::LEVEL && contains(FAN_LEVEL_IDS, property_id)) ||
        (targetType == TaskTargetType::LABEL && contains(FAN_LABEL_IDS, property_id))) {
      return std::unique_ptr<SetDataTask>(new SetDataTask(targetType, property_id, value));  // Valid task
    }
    return nullptr;  // Null unique_ptr if validation fails
  }

 private:
  SetDataTask(TaskTargetType targetType, int property_id, const char *value)
      : targetType(targetType), property_id(property_id) {
    std::strncpy(this->value, value, sizeof(this->value) - 1);
    this->value[sizeof(this->value) - 1] = '\0';  // Ensure null termination
  }
};

struct GetDataTask {
  TaskTargetType targetType;
  int property_id;
  TaskState state = TaskState::TO_BE_SENT;
  const TaskType taskType = TaskType::GET;

  static std::unique_ptr<GetDataTask> create(TaskTargetType targetType, int property_id) {
    if ((targetType == TaskTargetType::LEVEL && contains(FAN_LEVEL_IDS, property_id)) ||
        (targetType == TaskTargetType::LABEL && contains(FAN_LABEL_IDS, property_id))) {
      return std::unique_ptr<GetDataTask>(new GetDataTask(targetType, property_id));  // Valid task
    }
    return nullptr;  // Null unique_ptr if validation fails
  }

 private:
  GetDataTask(TaskTargetType targetType, int property_id) : targetType(targetType), property_id(property_id) {}
};

}  // namespace sec_touch
}  // namespace esphome