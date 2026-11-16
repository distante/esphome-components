#pragma once

#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <vector>
#include "esphome/components/uart/uart_component.h"
#include "esphome/components/sec_touch/sec_touch.h"

namespace esphome::sec_touch::testing {

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

/// Re-exports the hub internals the tests steer.
class TestableSECTouch : public SECTouchComponent {
 public:
  using SECTouchComponent::task_start_time_;
};

}  // namespace esphome::sec_touch::testing
