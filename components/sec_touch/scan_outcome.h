#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_scan_outcome.

#include <cstdint>

namespace esphome {
namespace sec_touch {

/// How long the hub waits for the reply to a task before its watchdog ends the task as TASK_END_TIMEOUT.
constexpr const unsigned long TASK_TIMEOUT_MS = 2000;

// Enumerators carry a prefix because _definitions.h #defines NAK (and other wire bytes).

/// How the hub's running task ended.
enum class TaskEnd : uint8_t {
  /// A data message arrived, or the ACK that completes a SET.
  TASK_END_ANSWERED,
  /// The panel refused the request.
  TASK_END_NAK,
  /// A reply arrived but could not be parsed.
  TASK_END_MALFORMED,
  /// No reply within TASK_TIMEOUT_MS.
  TASK_END_TIMEOUT,
};

/// Lowercase name of a TaskEnd, for logs.
inline const char *task_end_name(TaskEnd end) {
  switch (end) {
    case TaskEnd::TASK_END_ANSWERED:
      return "answered";
    case TaskEnd::TASK_END_NAK:
      return "nak";
    case TaskEnd::TASK_END_MALFORMED:
      return "malformed";
    case TaskEnd::TASK_END_TIMEOUT:
      return "timeout";
  }
  return "unknown";
}

/// The outcome of the latest scan query, as the sniffer records it in a property's query status.
enum class ScanOutcome : uint8_t { SCAN_OUTCOME_OK, SCAN_OUTCOME_NAK, SCAN_OUTCOME_TIMEOUT };

class ScanOutcomeTracker {
 public:
  /// Called by the hub whenever a task ends. Only the sniffer's own tasks (`sniffer_task`: its scan queries and write
  /// probe steps) that end in scan mode count: a write that runs during a scan leaves the outcome alone.
  void record(TaskEnd end, bool scan_mode, bool sniffer_task) {
    if (!scan_mode || !sniffer_task) {
      return;
    }
    this->last_ = outcome_of(end);
  }
  /// Called by the hub when scan mode turns on, so an earlier scan's outcome can't leak into it.
  void begin_scan() { this->last_ = ScanOutcome::SCAN_OUTCOME_OK; }
  ScanOutcome last() const { return this->last_; }

 protected:
  static ScanOutcome outcome_of(TaskEnd end) {
    switch (end) {
      case TaskEnd::TASK_END_ANSWERED:
        return ScanOutcome::SCAN_OUTCOME_OK;
      case TaskEnd::TASK_END_NAK:
        return ScanOutcome::SCAN_OUTCOME_NAK;
      case TaskEnd::TASK_END_MALFORMED:
        // No usable answer: retried like a timeout.
        return ScanOutcome::SCAN_OUTCOME_TIMEOUT;
      case TaskEnd::TASK_END_TIMEOUT:
        return ScanOutcome::SCAN_OUTCOME_TIMEOUT;
    }
    return ScanOutcome::SCAN_OUTCOME_TIMEOUT;
  }

  ScanOutcome last_{ScanOutcome::SCAN_OUTCOME_OK};
};

}  // namespace sec_touch
}  // namespace esphome
