#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "common.h"

namespace esphome::sec_touch::testing {

namespace {

/// Fan pair 1's level property.
constexpr int FAN_1_LEVEL_ID = FAN_LEVEL_IDS[0];

/// The reply for FAN_1_LEVEL_ID with one bit flipped ('7' -> '3'), captured on the installed controller (issue #43).
std::vector<uint8_t> bad_crc_frame() { return frame(data_head(133, "10") + "42696"); }

/// The panel's ACK with one bit flipped (issue #50): `02 07 0A`, too short to be a data frame.
std::vector<uint8_t> corrupted_ack() { return frame(std::string(1, static_cast<char>(ACK ^ 0x01))); }

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

  /// Host millis() is the real clock: ages the running task past the watchdog instead of sleeping.
  void age_task_past_watchdog() { this->hub_.task_start_time_ = millis() - (TASK_TIMEOUT_MS + 1); }

  /// Dispatches a GET for `property_id`, then feeds it a corrupted ACK. Afterwards tx holds only what the hub sent
  /// in reply to that ACK.
  void dispatch_get_then_corrupted_ack(int property_id) {
    this->hub_.add_discovery_get_task(property_id);
    this->hub_.loop();  // dispatches the GET
    this->uart_.tx.clear();
    this->uart_.push_rx(corrupted_ack());
    this->loop_times(2);
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
// the TASK_TIMEOUT_MS watchdog fires. It never resends a frame, so the ACK gives nothing up (PR #45, live device).
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
  this->uart_.push_rx(corrupted_ack());
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

  this->age_task_past_watchdog();
  this->hub_.loop();
  ASSERT_EQ(this->hub_.get_last_scan_outcome(), ScanOutcome::SCAN_OUTCOME_TIMEOUT);

  this->hub_.enter_scan_mode();

  EXPECT_EQ(this->hub_.get_last_scan_outcome(), ScanOutcome::SCAN_OUTCOME_OK);
}

// A corrupted ACK during a GET leaves the GET open: the data frame that follows answers it (issue #50).
TEST_F(SECTouchHubTest, CorruptedAckKeepsGetOpenUntilItsDataFrame) {
  const auto &seen = this->record_taps();
  this->dispatch_get_then_corrupted_ack(FAN_1_LEVEL_ID);

  EXPECT_EQ(this->hub_.current_running_task_type, TaskType::GET_DATA);
  EXPECT_TRUE(this->uart_.tx.empty());

  this->uart_.push_rx(data_frame(FAN_1_LEVEL_ID, "10"));
  this->loop_times(2);

  EXPECT_EQ(seen, std::vector<std::string>{std::to_string(FAN_1_LEVEL_ID) + "=10"});
  EXPECT_EQ(this->hub_.current_running_task_type, TaskType::NONE);
  EXPECT_EQ(this->uart_.tx, (std::vector<uint8_t>{STX, ACK, ETX}));
}

// The panel answered the scanned property, so the scan records it as answered, not as a timeout to retry.
// A scan reads the outcome when the queue empties, so the test reads it there too.
TEST_F(SECTouchHubTest, CorruptedAckDuringScanStillRecordsTheAnswer) {
  std::vector<ScanOutcome> outcomes;
  this->hub_.register_queue_empty_listener(
      [this, &outcomes]() { outcomes.push_back(this->hub_.get_last_scan_outcome()); });
  this->hub_.enter_scan_mode();
  this->dispatch_get_then_corrupted_ack(FAN_1_LEVEL_ID);
  this->uart_.push_rx(data_frame(FAN_1_LEVEL_ID, "10"));
  this->loop_times(2);

  EXPECT_EQ(outcomes, std::vector<ScanOutcome>{ScanOutcome::SCAN_OUTCOME_OK});
}

// If the data frame never comes, the GET stays bounded: the watchdog ends it like any unanswered GET.
TEST_F(SECTouchHubTest, CorruptedAckWithoutDataFrameEndsGetAtTheWatchdog) {
  this->hub_.enter_scan_mode();
  this->dispatch_get_then_corrupted_ack(FAN_1_LEVEL_ID);
  // The real check: the corrupted ACK did not end the GET, so only the watchdog can.
  ASSERT_EQ(this->hub_.current_running_task_type, TaskType::GET_DATA);

  this->age_task_past_watchdog();
  this->hub_.loop();

  EXPECT_EQ(this->hub_.current_running_task_type, TaskType::NONE);
  // Not proof of a watchdog end: a malformed end is recorded as TIMEOUT too (scan_outcome.h).
  EXPECT_EQ(this->hub_.get_last_scan_outcome(), ScanOutcome::SCAN_OUTCOME_TIMEOUT);
}

// A SET has no data frame to wait for: its ACK is the whole answer, so a corrupted one ends it as malformed.
TEST_F(SECTouchHubTest, CorruptedAckEndsSetAsMalformed) {
  this->hub_.enter_scan_mode();  // only so the malformed end is recorded and visible
  this->hub_.add_set_task(SetDataTask::create(TaskTargetType::LEVEL, FAN_1_LEVEL_ID, "5"));
  this->hub_.loop();  // dispatches the SET
  ASSERT_EQ(this->hub_.current_running_task_type, TaskType::SET_DATA);
  this->uart_.tx.clear();

  this->uart_.push_rx(corrupted_ack());
  this->loop_times(2);

  // The real check: the SET ended at once, with no watchdog. The outcome reads TIMEOUT because a malformed end
  // is recorded as TIMEOUT (scan_outcome.h).
  EXPECT_EQ(this->hub_.current_running_task_type, TaskType::NONE);
  EXPECT_EQ(this->hub_.get_last_scan_outcome(), ScanOutcome::SCAN_OUTCOME_TIMEOUT);
  EXPECT_TRUE(this->uart_.tx.empty());
}

}  // namespace esphome::sec_touch::testing
