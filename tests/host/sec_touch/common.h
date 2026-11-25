#pragma once

#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include "esphome/components/uart/uart_component.h"
#include "esphome/components/sec_touch/sec_touch.h"
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

/// Re-exports the hub internals the tests steer. task_start_time_ lets a test age a task past the watchdog,
/// since the host platform's millis() is the real clock. current_running_task_type shows whether a task is still open.
/// The last_*_ms_ members let a test age a poll's interval the same way.
class TestableSECTouch : public SECTouchComponent {
 public:
  using SECTouchComponent::current_running_task_type;
  using SECTouchComponent::last_global_settings_update_ms_;
  using SECTouchComponent::last_time_programs_update_ms_;
  using SECTouchComponent::task_start_time_;
};

/// `[STX][NAK][ETX]`: the panel refuses a request.
inline std::vector<uint8_t> nak_frame() { return frame(std::string(1, static_cast<char>(NAK))); }

/// The panel's ACK with one bit flipped (issue #50): `02 07 0A`, too short to be a data frame.
inline std::vector<uint8_t> corrupted_ack() { return frame(std::string(1, static_cast<char>(ACK ^ 0x01))); }

}  // namespace esphome::sec_touch::testing
