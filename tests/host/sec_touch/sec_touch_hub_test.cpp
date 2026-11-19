#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "common.h"

namespace esphome::sec_touch::testing {

namespace {

/// The reply for 173 with one bit flipped ('7' -> '3'), captured on the installed controller (issue #43).
std::vector<uint8_t> bad_crc_frame() { return frame(data_head(133, "10") + "42696"); }

class SECTouchHubTest : public ::testing::Test {
 protected:
  void SetUp() override { this->hub_.set_uart_parent(&this->uart_); }

  /// Records every message the hub routes to a tap as "<property_id>=<raw value>".
  std::vector<std::string> &record_taps() {
    this->hub_.register_message_tap([this](int, int property_id, const std::string &raw) {
      this->seen_.push_back(std::to_string(property_id) + "=" + raw);
    });
    return this->seen_;
  }

  void loop_times(int n) {
    for (int i = 0; i < n; i++) {
      this->hub_.loop();
    }
  }

  FakeUART uart_;
  TestableSECTouch hub_;
  std::vector<std::string> seen_;
};

}  // namespace

TEST_F(SECTouchHubTest, DataFrameReachesTapByteExactAndIsAcked) {
  const auto &seen = this->record_taps();

  this->uart_.push_rx(data_frame(DATE_PROPERTY_ID, "16.11.26"));
  this->loop_times(3);

  EXPECT_EQ(seen, std::vector<std::string>{"1=16.11.26"});
  EXPECT_EQ(this->uart_.tx, (std::vector<uint8_t>{STX, ACK, ETX}));
}

// A dropped data frame is still ACKed: without the ACK the panel ignores the next request until
// the 2 s watchdog fires. It never resends a frame, so the ACK gives nothing up (PR #45, live device).
TEST_F(SECTouchHubTest, FrameWithBadCrcNeverReachesTapButIsAcked) {
  const auto &seen = this->record_taps();

  this->uart_.push_rx(bad_crc_frame());
  this->loop_times(3);

  EXPECT_TRUE(seen.empty());
  EXPECT_EQ(this->uart_.tx, (std::vector<uint8_t>{STX, ACK, ETX}));
}

TEST_F(SECTouchHubTest, FrameWithoutCrcIsDroppedByDefaultButAcked) {
  const auto &seen = this->record_taps();

  this->uart_.push_rx(frame("32" + tab + "173" + tab + "10"));
  this->loop_times(3);

  EXPECT_TRUE(seen.empty());
  EXPECT_EQ(this->uart_.tx, (std::vector<uint8_t>{STX, ACK, ETX}));
}

// A frame too short to be data may be the panel's own ACK with a flipped bit, sent before the data
// frame. ACKing it would answer a frame that has not arrived yet.
TEST_F(SECTouchHubTest, FrameTooShortForDataIsNotAcked) {
  this->uart_.push_rx(frame(std::string(1, static_cast<char>(ACK ^ 0x01))));
  this->loop_times(2);

  EXPECT_TRUE(this->uart_.tx.empty());
}

TEST_F(SECTouchHubTest, FrameWithoutCrcReachesTapWithOptionalCrc) {
  this->hub_.set_optional_crc(true);
  const auto &seen = this->record_taps();

  this->uart_.push_rx(frame("32" + tab + "173" + tab + "10"));
  this->loop_times(3);

  EXPECT_EQ(seen, std::vector<std::string>{"173=10"});
}

TEST_F(SECTouchHubTest, NakDuringScanRecordsNakOutcome) {
  this->hub_.enter_scan_mode();

  this->uart_.push_rx(frame(std::string(1, static_cast<char>(NAK))));
  this->loop_times(2);

  EXPECT_EQ(this->hub_.get_last_scan_outcome(), ScanOutcome::SCAN_OUTCOME_NAK);
}

// A bad CRC ends the task as malformed, which a scan records as TIMEOUT, the same as no reply (issue #43).
TEST_F(SECTouchHubTest, BadCrcDuringScanRecordsTimeoutOutcome) {
  this->hub_.enter_scan_mode();

  this->uart_.push_rx(bad_crc_frame());
  this->loop_times(3);

  EXPECT_EQ(this->hub_.get_last_scan_outcome(), ScanOutcome::SCAN_OUTCOME_TIMEOUT);
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
