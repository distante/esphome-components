#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "common.h"

namespace esphome::sec_touch::testing {

namespace {

std::vector<uint8_t> frame(const std::string &body) {
  std::vector<uint8_t> bytes{STX};
  bytes.insert(bytes.end(), body.begin(), body.end());
  bytes.push_back(ETX);
  return bytes;
}

/// `[STX]32[TAB]<id>[TAB]<value>[TAB]<crc>[ETX]`, the data half of a GET reply.
std::vector<uint8_t> data_frame(int property_id, const std::string &value) {
  std::string tab(1, static_cast<char>(TAB));
  return frame("32" + tab + std::to_string(property_id) + tab + value + tab + "12345");
}

class SECTouchHubTest : public ::testing::Test {
 protected:
  void SetUp() override { this->hub_.set_uart_parent(&this->uart_); }

  void loop_times(int n) {
    for (int i = 0; i < n; i++) {
      this->hub_.loop();
    }
  }

  FakeUART uart_;
  TestableSECTouch hub_;
};

}  // namespace

TEST_F(SECTouchHubTest, DataFrameReachesTapByteExactAndIsAcked) {
  std::vector<std::string> seen;
  this->hub_.register_message_tap([&seen](int, int property_id, const std::string &raw) {
    seen.push_back(std::to_string(property_id) + "=" + raw);
  });

  this->uart_.push_rx(data_frame(DATE_PROPERTY_ID, "16.11.26"));
  this->loop_times(3);

  EXPECT_EQ(seen, std::vector<std::string>{"1=16.11.26"});
  EXPECT_EQ(this->uart_.tx, (std::vector<uint8_t>{STX, ACK, ETX}));
}

TEST_F(SECTouchHubTest, NakDuringScanRecordsNakOutcome) {
  this->hub_.enter_scan_mode();

  this->uart_.push_rx(frame(std::string(1, static_cast<char>(NAK))));
  this->loop_times(2);

  EXPECT_EQ(this->hub_.get_last_scan_outcome(), ScanOutcome::SCAN_OUTCOME_NAK);
}

TEST_F(SECTouchHubTest, EnteringScanModeClearsStaleTimeoutOutcome) {
  this->hub_.enter_scan_mode();
  this->hub_.add_discovery_get_task(500);
  this->hub_.loop();  // dispatches the GET
  ASSERT_FALSE(this->uart_.tx.empty());

  // Host millis() is the real clock: age the task past the hub's 2000 ms TASK_TIMEOUT_MS instead of sleeping.
  this->hub_.task_start_time_ = millis() - 2001;
  this->hub_.loop();
  ASSERT_EQ(this->hub_.get_last_scan_outcome(), ScanOutcome::SCAN_OUTCOME_TIMEOUT);

  this->hub_.enter_scan_mode();

  EXPECT_EQ(this->hub_.get_last_scan_outcome(), ScanOutcome::SCAN_OUTCOME_OK);
}

}  // namespace esphome::sec_touch::testing
