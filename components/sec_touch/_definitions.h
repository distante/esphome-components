#pragma once

#include <map>
#include <memory>
#include <string>
#include <cstring>
#include "esphome/core/log.h"
#include <functional>
#include "protocol_ids.h"
#include "data_message.h"
#include "scan_outcome.h"
#include "time_program_ids.h"

namespace esphome {
namespace sec_touch {

#define STX WIRE_STX  // Start of Text
#define ETX WIRE_ETX  // End of a frame: a line feed, see WIRE_ETX
#define ACK WIRE_ACK  // Acknowledge
#define NAK WIRE_NAK  // Not Acknowledge
#define TAB WIRE_TAB  // Tab
#define NOISE 0xFF    // 255

/**
 * Callback for when a property is updated
 * @param property_id The ID of the property that was updated for example 173 for level of the fan 1
 * @param new_value The new value of the property obtained from the touch controller
 */
using UpdateCallbackListener = std::function<void(int property_id, int new_value)>;

// For properties whose value is not an integer on the wire (dates, clock), which the
// int listeners only see truncated by atoi.
using StringUpdateCallbackListener = std::function<void(int property_id, const std::string &raw_value)>;

// Called when the task queue is empty and no task is awaiting a response
using QueueEmptyListener = std::function<void()>;

struct BaseTask;
/// Called once for every task that ends: answered, refused, malformed (after its one retry, if any), timed out, or
/// dropped from the queue when scan mode starts (as a timeout). The task is gone once the listener returns.
using TaskEndListener = std::function<void(const BaseTask &task, TaskEnd end)>;

/**
 * Used to couple `SetDataTask->id` with the level_id or label_id for example
 */
enum class TaskTargetType {
  LEVEL,
  LABEL,
  GLOBAL_SETTING,
  TIME_PROGRAM,
  /// The sniffer's own task (create_unchecked): a scan query or a write probe step. Only these set the scan outcome.
  SNIFFER,
};
enum class TaskType {
  SET_DATA,
  GET_DATA,
  /**
   * Used to indicate that no task is currently being processed
   */
  NONE
};

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
      case TaskTargetType::GLOBAL_SETTING:
        return "GLOBAL_SETTING";
      case TaskTargetType::TIME_PROGRAM:
        return "TIME_PROGRAM";
      case TaskTargetType::SNIFFER:
        return "SNIFFER";
      default:
        return "UNKNOWN";
    }
  }

  static const char *TaskType(TaskType v) {
    switch (v) {
      case TaskType::SET_DATA:
        return "SET_DATA";
      case TaskType::GET_DATA:
        return "GET_DATA";
      case TaskType::NONE:
        return "NONE";
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
  char buffer[FRAME_BUFFER_SIZE];
  int buffer_index = -1;

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
    if (this->buffer_index + 1 < (int) sizeof(this->buffer) - 1) {
      this->buffer_index++;
      this->buffer[this->buffer_index] = data;

    } else {
      ESP_LOGW("sec_touch.incoming", "  Buffer overflow! Discarding extra data.");
    }

    return this->buffer_index;
  }
};

struct BaseTask {
  virtual ~BaseTask() = default;
  virtual TaskType get_task_type() const = 0;
  TaskTargetType targetType;
  int property_id;
  /// Already sent once more after its reply was dropped (should_retry_get).
  bool retried{false};
  /// Dropped by the hub when scan mode started or stopped: it ends as a timeout, unsent or with its answer cut off.
  bool dropped{false};
  /// Tells the task apart from other tasks of its ID when it ends: a priority GET (add_priority_get_task) and a
  /// latest-wins SET (add_set_task_latest_wins) carry one; 0 for other tasks.
  uint32_t ticket{0};
};

struct SetDataTask : public BaseTask {
  char value[SET_VALUE_CAP + 1];

  static std::unique_ptr<SetDataTask> create(TaskTargetType targetType, int property_id, const char *value) {
    if ((targetType == TaskTargetType::LEVEL && contains(FAN_LEVEL_IDS, property_id)) ||
        (targetType == TaskTargetType::LABEL && contains(FAN_LABEL_IDS, property_id)) ||
        (targetType == TaskTargetType::GLOBAL_SETTING && contains(GLOBAL_SETTING_IDS, property_id)) ||
        (targetType == TaskTargetType::TIME_PROGRAM && time_program_slot(property_id).has_value())) {
      return std::unique_ptr<SetDataTask>(new SetDataTask(targetType, property_id, value));  // Valid task
    }
    return nullptr;  // Null unique_ptr if validation fails
  }

  // Bypasses ID validation — for the sniffer's write probe only. A value over SET_VALUE_CAP is cut there.
  static std::unique_ptr<SetDataTask> create_unchecked(int property_id, const char *value) {
    return std::unique_ptr<SetDataTask>(new SetDataTask(TaskTargetType::SNIFFER, property_id, value));
  }

  TaskType get_task_type() const override { return TaskType::SET_DATA; }

  /// Replaces the value to send; one over SET_VALUE_CAP is cut.
  void set_value(const char *value) {
    std::strncpy(this->value, value, sizeof(this->value) - 1);
    this->value[sizeof(this->value) - 1] = '\0';  // Ensure null termination
  }

 private:
  SetDataTask(TaskTargetType targetType, int property_id, const char *value) {
    this->targetType = targetType;
    this->property_id = property_id;
    this->set_value(value);
  }
};

struct GetDataTask : public BaseTask {
  static std::unique_ptr<GetDataTask> create(TaskTargetType targetType, int property_id) {
    if ((targetType == TaskTargetType::LEVEL && contains(FAN_LEVEL_IDS, property_id)) ||
        (targetType == TaskTargetType::LABEL && contains(FAN_LABEL_IDS, property_id)) ||
        (targetType == TaskTargetType::GLOBAL_SETTING && contains(GLOBAL_SETTING_IDS, property_id)) ||
        (targetType == TaskTargetType::TIME_PROGRAM && time_program_slot(property_id).has_value())) {
      return std::unique_ptr<GetDataTask>(new GetDataTask(targetType, property_id));  // Valid task
    }
    return nullptr;  // Null unique_ptr if validation fails
  }

  // Bypasses ID validation — for sniffer/discovery use only
  static std::unique_ptr<GetDataTask> create_unchecked(int property_id) {
    return std::unique_ptr<GetDataTask>(new GetDataTask(TaskTargetType::SNIFFER, property_id));
  }

  TaskType get_task_type() const override { return TaskType::GET_DATA; }

 private:
  GetDataTask(TaskTargetType targetType, int property_id) {
    this->targetType = targetType;
    this->property_id = property_id;
  }
};

}  // namespace sec_touch
}  // namespace esphome
