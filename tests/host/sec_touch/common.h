#pragma once

#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include "esphome/components/logger/logger.h"
#include "esphome/components/uart/uart_component.h"
#include "esphome/components/sec_touch/sec_touch.h"
#include "esphome/components/sec_touch/time_program_codec.h"
#include "esphome/components/sec_touch/XModemCRC.h"

namespace esphome::sec_touch::testing {

/// `[STX]<body>[ETX]`.
inline std::vector<uint8_t> frame(const std::string &body) {
  std::vector<uint8_t> bytes{STX};
  bytes.insert(bytes.end(), body.begin(), body.end());
  bytes.push_back(ETX);
  return bytes;
}

inline const std::string tab(1, static_cast<char>(TAB));

/// `32[TAB]<id>[TAB]<value>[TAB]`: the part of a data message the CRC covers, after STX.
inline std::string data_head(int property_id, const std::string &value) {
  return std::to_string(COMMANDID_SET) + tab + std::to_string(property_id) + tab + value + tab;
}

/// `[STX]32[TAB]<id>[TAB]<value>[TAB]<crc>[ETX]`, the data half of a GET reply, with its real XModem CRC.
inline std::vector<uint8_t> data_frame(int property_id, const std::string &value) {
  std::string covered = std::string(1, static_cast<char>(STX)) + data_head(property_id, value);
  unsigned short crc = GetXModemCRC(covered.data(), covered.size());
  return frame(data_head(property_id, value) + std::to_string(crc));
}

/// `[STX][ACK][ETX]`: the panel's ACK of a request, and the hub's ACK of a data frame.
inline std::vector<uint8_t> ack_frame() { return frame(std::string(1, static_cast<char>(ACK))); }

/// `[STX]32800[TAB]`: how every GET the hub sends starts.
inline const std::string get_start = std::string(1, static_cast<char>(STX)) + std::to_string(COMMANDID_GET) + tab;

/// `[STX]32800[TAB]<id>[TAB]<crc>[ETX]`, the GET the hub sends.
inline std::vector<uint8_t> get_frame(int property_id) {
  std::string covered = get_start + std::to_string(property_id) + tab;
  unsigned short crc = GetXModemCRC(covered.data(), covered.size());
  std::string bytes = covered + std::to_string(crc) + static_cast<char>(ETX);
  return {bytes.begin(), bytes.end()};
}

/// `[STX]32[TAB]<id>[TAB]<value>[TAB]<crc>[ETX]`, the SET the hub sends.
/// Same layout as a data frame: COMMANDID_SET is 32.
inline std::vector<uint8_t> set_frame(int property_id, const std::string &value) {
  return data_frame(property_id, value);
}

/// `a` followed by `b`, such as an ACK and the data frame after it.
inline std::vector<uint8_t> concat(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
  std::vector<uint8_t> out = a;
  out.insert(out.end(), b.begin(), b.end());
  return out;
}

/// The raw value of enabled mask `mask`, as the hub writes it and the panel sends it.
inline std::string mask_text(uint8_t mask) { return std::string(format_enabled_mask(mask).data()); }

/// A UART bus whose rx bytes the test pushes and whose tx bytes the test reads.
class FakeUART : public uart::UARTComponent {
 public:
  std::vector<uint8_t> tx;
  std::vector<uint8_t> rx;

  void push_rx(const std::vector<uint8_t> &data) { this->rx.insert(this->rx.end(), data.begin(), data.end()); }

  void write_array(const uint8_t *data, size_t len) override { this->tx.insert(this->tx.end(), data, data + len); }

  bool peek_byte(uint8_t *data) override {
    if (this->rx.empty()) {
      return false;
    }
    *data = this->rx.front();
    return true;
  }

  bool read_array(uint8_t *data, size_t len) override {
    if (this->rx.size() < len) {
      return false;
    }
    std::copy(this->rx.begin(), this->rx.begin() + len, data);
    this->rx.erase(this->rx.begin(), this->rx.begin() + len);
    return true;
  }

  size_t available() override { return this->rx.size(); }
  uart::UARTFlushResult flush() override { return uart::UARTFlushResult::UART_FLUSH_RESULT_SUCCESS; }

 protected:
  void check_logger_conflict() override {}
};

/// The poll intervals every test hub runs with. Codegen always sets them and their defaults live only in the YAML
/// schema, so these are test values, not the defaults.
constexpr uint32_t TEST_GLOBAL_SETTINGS_INTERVAL_MS = 20000;
constexpr uint32_t TEST_TIME_PROGRAMS_INTERVAL_MS = 240000;

/// Re-exports the hub internals the tests steer. current_running_task_type shows whether a task is still open. The
/// host platform's millis() is the real clock, so the last_*_ms_ members let a test age a poll's interval instead of
/// sleeping, as age_task_past_watchdog() does for a task. data_task_queue and time_program_polls_ let a test check and
/// bend the queue.
class TestableSECTouch : public SECTouchComponent {
 public:
  TestableSECTouch() {
    this->set_global_settings_update_interval_ms(TEST_GLOBAL_SETTINGS_INTERVAL_MS);
    this->set_time_programs_update_interval_ms(TEST_TIME_PROGRAMS_INTERVAL_MS);
  }

  using SECTouchComponent::current_running_task_type;
  using SECTouchComponent::data_task_queue;
  using SECTouchComponent::time_program_polls_;
  using SECTouchComponent::last_global_settings_update_ms_;
  using SECTouchComponent::last_time_programs_update_ms_;

  /// IDs whose priority GET can't be queued: add_priority_get_task() returns 0 for them, as for an ID it doesn't
  /// accept.
  std::set<int> unqueueable_priority_gets;
  uint32_t add_priority_get_task(TaskTargetType target, int property_id) override {
    if (this->unqueueable_priority_gets.count(property_id) > 0) {
      return 0;
    }
    return SECTouchComponent::add_priority_get_task(target, property_id);
  }

  /// IDs whose latest-wins SET can't be queued: add_set_task_latest_wins() returns 0 for them, as for an ID it doesn't
  /// accept.
  std::set<int> unqueueable_sets;
  uint32_t add_set_task_latest_wins(TaskTargetType target, int property_id, const char *value) override {
    if (this->unqueueable_sets.count(property_id) > 0) {
      return 0;
    }
    return SECTouchComponent::add_set_task_latest_wins(target, property_id, value);
  }

  void loop_times(int n) {
    for (int i = 0; i < n; i++) {
      this->loop();
    }
  }

  /// Host millis() is the real clock: ages the running task past the watchdog instead of sleeping.
  void age_task_past_watchdog() { this->task_start_time_ = millis() - (TASK_TIMEOUT_MS + 1); }
};

/// `[STX][NAK][ETX]`: the panel refuses a request.
inline std::vector<uint8_t> nak_frame() { return frame(std::string(1, static_cast<char>(NAK))); }

/// The panel's ACK with one bit flipped (issue #50): `02 07 0A`, too short to be a data frame.
inline std::vector<uint8_t> corrupted_ack() { return frame(std::string(1, static_cast<char>(ACK ^ 0x01))); }

/// The reply's STX as the controller received it, garbled to 0x00, so every byte reads as noise.
inline std::vector<uint8_t> without_stx(std::vector<uint8_t> f) {
  f[0] = 0x00;
  return f;
}

/// Every log line written since the last clear(). One instance for the whole run: the logger can't drop a callback,
/// and the build has one slot for it (tests/host/sec_touch/__init__.py).
class LogCapture {
 public:
  static LogCapture &instance() {
    static LogCapture capture;
    return capture;
  }

  void clear() { this->lines_.clear(); }

  /// How many lines contain `text`.
  int count(const std::string &text) const {
    return static_cast<int>(std::count_if(this->lines_.begin(), this->lines_.end(), [&text](const std::string &line) {
      return line.find(text) != std::string::npos;
    }));
  }

 private:
  LogCapture() {
    logger::global_logger->add_log_callback(this,
                                            [](void *self, uint8_t, const char *, const char *message, size_t len) {
                                              static_cast<LogCapture *>(self)->lines_.emplace_back(message, len);
                                            });
  }

  std::vector<std::string> lines_;
};

}  // namespace esphome::sec_touch::testing
