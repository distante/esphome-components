#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "common.h"

namespace esphome::sec_touch::testing {

namespace {

/// Fan pair 1's level property.
constexpr int FAN_1_LEVEL_ID = FAN_LEVEL_IDS[0];

/// A property id the panel never answers, so its GET always runs into the watchdog.
constexpr int UNANSWERED_PROPERTY_ID = 500;

/// `[STX]32[TAB]<id>[TAB]<value>[ETX]`: a data frame without its CRC field.
std::vector<uint8_t> frame_without_crc(int property_id, const std::string &value) {
  return frame(std::to_string(COMMANDID_SET) + tab + std::to_string(property_id) + tab + value);
}

/// The reply for FAN_1_LEVEL_ID with one bit flipped ('7' -> '3'), captured on the installed controller (issue #43).
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

/// A data frame for `property_id` whose CRC field has one bit flipped.
std::vector<uint8_t> data_frame_with_bad_crc(int property_id, const std::string &value) {
  std::string covered = std::string(1, static_cast<char>(STX)) + data_head(property_id, value);
  unsigned short crc = GetXModemCRC(covered.data(), covered.size()) ^ 0x01;
  return frame(data_head(property_id, value) + std::to_string(crc));
}

std::vector<uint8_t> concat(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
  std::vector<uint8_t> out = a;
  out.insert(out.end(), b.begin(), b.end());
  return out;
}

constexpr int LABEL_1_ID = FAN_LABEL_IDS[0];
constexpr int LABEL_2_ID = FAN_LABEL_IDS[1];
constexpr int LABEL_3_ID = FAN_LABEL_IDS[2];

/// A GET whose reply is dropped as malformed is sent once more (issue #52).
class SECTouchGetRetryTest : public SECTouchHubTest {
 protected:
  void SetUp() override {
    SECTouchHubTest::SetUp();
    this->hub_.set_inter_task_delay(0);
  }

  /// Queues a label GET for each of `ids`. Each label's listener records "<property_id>=<value>" in labels_.
  void queue_label_reads(const std::vector<int> &ids) {
    for (int id : ids) {
      this->hub_.register_manual_update_listener(id, [this](int property_id, int value) {
        this->labels_.push_back(std::to_string(property_id) + "=" + std::to_string(value));
      });
    }
    this->hub_.add_manual_tasks_to_queue();
  }

  /// Sends the first queued task. Afterwards tx holds only that request.
  void send_next_task() {
    this->uart_.tx.clear();
    this->hub_.loop();
  }

  /// The panel answers the GET in flight with its ACK, then `data`. Returns what the hub sent meanwhile: its ACK of
  /// `data`, then the next task it dispatched.
  std::vector<uint8_t> panel_answers(const std::vector<uint8_t> &data) {
    this->uart_.tx.clear();
    this->uart_.push_rx(ack_frame());
    this->uart_.push_rx(data);
    this->loop_times(3);
    return this->uart_.tx;
  }

  std::vector<std::string> labels_;
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

  this->uart_.push_rx(frame_without_crc(FAN_1_LEVEL_ID, "10"));
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

  this->uart_.push_rx(frame_without_crc(FAN_1_LEVEL_ID, "10"));
  this->loop_times(3);

  EXPECT_EQ(seen, std::vector<std::string>{std::to_string(FAN_1_LEVEL_ID) + "=10"});
}

TEST_F(SECTouchHubTest, NakDuringScanRecordsNakOutcome) {
  this->hub_.enter_scan_mode();

  this->uart_.push_rx(nak_frame());
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
  this->hub_.add_discovery_get_task(UNANSWERED_PROPERTY_ID);
  this->hub_.loop();  // dispatches the GET
  ASSERT_FALSE(this->uart_.tx.empty());

  this->age_task_past_watchdog();
  this->hub_.loop();
  ASSERT_EQ(this->hub_.get_last_scan_outcome(), ScanOutcome::SCAN_OUTCOME_TIMEOUT);

  this->hub_.enter_scan_mode();

  EXPECT_EQ(this->hub_.get_last_scan_outcome(), ScanOutcome::SCAN_OUTCOME_OK);
}

// The write probe's SET of an unknown ID: only the sniffer's scan mode may send it.
TEST_F(SECTouchHubTest, UncheckedSetOutsideScanModeQueuesNothing) {
  EXPECT_FALSE(this->hub_.add_discovery_set_task(UNANSWERED_PROPERTY_ID, "03"));
  this->hub_.loop();

  EXPECT_TRUE(this->uart_.tx.empty());
  EXPECT_EQ(this->hub_.current_running_task_type, TaskType::NONE);
}

TEST_F(SECTouchHubTest, UncheckedSetInScanModeIsSentAndItsAckRecorded) {
  std::vector<ScanOutcome> outcomes;
  this->hub_.register_queue_empty_listener(
      [this, &outcomes]() { outcomes.push_back(this->hub_.get_last_scan_outcome()); });
  this->hub_.enter_scan_mode();
  this->hub_.loop();  // the queue-empty callback of entering scan mode
  outcomes.clear();

  EXPECT_TRUE(this->hub_.add_discovery_set_task(UNANSWERED_PROPERTY_ID, "03"));
  this->hub_.loop();  // dispatches the SET
  EXPECT_EQ(this->uart_.tx, set_frame(UNANSWERED_PROPERTY_ID, "03"));

  this->uart_.push_rx(ack_frame());
  this->loop_times(2);

  EXPECT_EQ(outcomes, std::vector<ScanOutcome>{ScanOutcome::SCAN_OUTCOME_OK});
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

// A label is read only at boot and on the update button, so a dropped reply would leave it empty until then.
TEST_F(SECTouchGetRetryTest, MalformedLabelReplyIsAckedThenTheSameGetIsSentAgain) {
  this->queue_label_reads({LABEL_1_ID});
  this->send_next_task();
  ASSERT_EQ(this->uart_.tx, get_frame(LABEL_1_ID));

  EXPECT_EQ(this->panel_answers(data_frame_with_bad_crc(LABEL_1_ID, "3")), concat(ack_frame(), get_frame(LABEL_1_ID)));

  this->panel_answers(data_frame(LABEL_1_ID, "3"));
  EXPECT_EQ(this->labels_, std::vector<std::string>{std::to_string(LABEL_1_ID) + "=3"});
}

// One retry per task, so a noisy line can't loop: the second bad reply ends the task and the next one goes out.
TEST_F(SECTouchGetRetryTest, SecondMalformedReplyEndsTheTaskWithoutAThirdSend) {
  this->queue_label_reads({LABEL_1_ID, LABEL_2_ID});
  this->send_next_task();
  this->panel_answers(data_frame_with_bad_crc(LABEL_1_ID, "3"));

  EXPECT_EQ(this->panel_answers(data_frame_with_bad_crc(LABEL_1_ID, "3")), concat(ack_frame(), get_frame(LABEL_2_ID)));
  EXPECT_TRUE(this->labels_.empty());
}

// No reply at all may mean a busy panel, where a retry only adds TASK_TIMEOUT_MS of waiting.
TEST_F(SECTouchGetRetryTest, WatchdogTimeoutIsNotRetried) {
  this->queue_label_reads({LABEL_1_ID, LABEL_2_ID});
  this->send_next_task();

  this->uart_.tx.clear();
  this->age_task_past_watchdog();
  this->hub_.loop();

  EXPECT_EQ(this->uart_.tx, get_frame(LABEL_2_ID));
}

// The sniffer retries a scan query itself, after the TIMEOUT outcome a malformed reply records.
TEST_F(SECTouchGetRetryTest, MalformedReplyDuringScanIsNotRetried) {
  this->hub_.enter_scan_mode();
  this->hub_.add_discovery_get_task(FAN_1_LEVEL_ID);
  this->hub_.add_discovery_get_task(UNANSWERED_PROPERTY_ID);
  this->send_next_task();

  EXPECT_EQ(this->panel_answers(data_frame_with_bad_crc(FAN_1_LEVEL_ID, "10")),
            concat(ack_frame(), get_frame(UNANSWERED_PROPERTY_ID)));
  EXPECT_EQ(this->hub_.get_last_scan_outcome(), ScanOutcome::SCAN_OUTCOME_TIMEOUT);
}

// The sniffer's stop can end scan mode while a scan query is still in flight. Its late reply belongs to the scan.
TEST_F(SECTouchGetRetryTest, MalformedReplyAfterScanStoppedIsNotRetried) {
  this->hub_.enter_scan_mode();
  this->hub_.add_discovery_get_task(FAN_1_LEVEL_ID);
  this->send_next_task();
  this->hub_.exit_scan_mode();

  EXPECT_EQ(this->panel_answers(data_frame_with_bad_crc(FAN_1_LEVEL_ID, "10")), ack_frame());
}

// Without a reply, the watchdog still ends the scan query, so polling goes on after the scan. Until then nothing
// else goes out.
TEST_F(SECTouchGetRetryTest, WatchdogEndsAScanQueryStillInFlightAfterScanStopped) {
  this->hub_.enter_scan_mode();
  this->hub_.add_discovery_get_task(FAN_1_LEVEL_ID);
  this->send_next_task();
  this->hub_.exit_scan_mode();
  this->queue_label_reads({LABEL_1_ID});

  this->send_next_task();
  EXPECT_TRUE(this->uart_.tx.empty());

  this->age_task_past_watchdog();
  this->hub_.loop();

  EXPECT_EQ(this->uart_.tx, get_frame(LABEL_1_ID));
}

// The late reply to the scan query dropped at scan exit ends that query, not the next task.
TEST_F(SECTouchGetRetryTest, ALateReplyAfterScanStoppedEndsTheDroppedQuery) {
  this->hub_.enter_scan_mode();
  this->hub_.add_discovery_get_task(FAN_1_LEVEL_ID);
  this->send_next_task();
  this->hub_.exit_scan_mode();
  this->queue_label_reads({LABEL_1_ID});
  this->send_next_task();

  EXPECT_EQ(this->panel_answers(data_frame(FAN_1_LEVEL_ID, "10")), concat(ack_frame(), get_frame(LABEL_1_ID)));
  EXPECT_TRUE(this->labels_.empty());
}

TEST_F(SECTouchGetRetryTest, QueuedTasksKeepTheirOrderBehindTheRetry) {
  this->queue_label_reads({LABEL_1_ID, LABEL_2_ID, LABEL_3_ID});
  this->send_next_task();

  EXPECT_EQ(this->panel_answers(data_frame_with_bad_crc(LABEL_1_ID, "1")), concat(ack_frame(), get_frame(LABEL_1_ID)));
  EXPECT_EQ(this->panel_answers(data_frame(LABEL_1_ID, "1")), concat(ack_frame(), get_frame(LABEL_2_ID)));
  EXPECT_EQ(this->panel_answers(data_frame(LABEL_2_ID, "2")), concat(ack_frame(), get_frame(LABEL_3_ID)));
  this->panel_answers(data_frame(LABEL_3_ID, "3"));

  EXPECT_EQ(this->labels_,
            (std::vector<std::string>{std::to_string(LABEL_1_ID) + "=1", std::to_string(LABEL_2_ID) + "=2",
                                      std::to_string(LABEL_3_ID) + "=3"}));
}

// Two entities may read one property (the clock for clock_drift and for live state, #130): both get each value,
// and the poll still sends one GET of it.
TEST_F(SECTouchGetRetryTest, TwoListenersOnOneIdBothGetItsValueFromOneGet) {
  std::vector<std::string> first;
  std::vector<std::string> second;
  this->hub_.register_global_setting_string_listener(CLOCK_PROPERTY_ID,
                                                     [&first](int, const std::string &raw) { first.push_back(raw); });
  this->hub_.register_global_setting_string_listener(CLOCK_PROPERTY_ID,
                                                     [&second](int, const std::string &raw) { second.push_back(raw); });
  this->hub_.add_global_setting_tasks_to_queue();
  this->send_next_task();
  ASSERT_EQ(this->uart_.tx, get_frame(CLOCK_PROPERTY_ID));

  EXPECT_EQ(this->panel_answers(data_frame(CLOCK_PROPERTY_ID, "08:30:00")), ack_frame());
  EXPECT_EQ(first, std::vector<std::string>{"08:30:00"});
  EXPECT_EQ(second, std::vector<std::string>{"08:30:00"});
}

/// Records every task end as "<GET|SET> <property_id> <end>".
class SECTouchTaskEndTest : public SECTouchGetRetryTest {
 protected:
  void SetUp() override {
    SECTouchGetRetryTest::SetUp();
    this->hub_.register_task_end_listener([this](const BaseTask &task, TaskEnd end) {
      this->ends_.push_back(std::string(task.get_task_type() == TaskType::SET_DATA ? "SET " : "GET ") +
                            std::to_string(task.property_id) + " " + task_end_name(end));
    });
  }

  std::string end_of(const char *kind, int property_id, TaskEnd end) {
    return std::string(kind) + " " + std::to_string(property_id) + " " + task_end_name(end);
  }

  std::vector<std::string> ends_;
};

TEST_F(SECTouchTaskEndTest, AnAnsweredGetAndSetEachEndOnce) {
  this->queue_label_reads({LABEL_1_ID});
  this->hub_.add_set_task(SetDataTask::create(TaskTargetType::LEVEL, FAN_1_LEVEL_ID, "5"));
  this->send_next_task();
  this->uart_.push_rx(ack_frame());
  this->loop_times(2);
  this->panel_answers(data_frame(LABEL_1_ID, "3"));

  EXPECT_EQ(this->ends_, (std::vector<std::string>{end_of("SET", FAN_1_LEVEL_ID, TaskEnd::TASK_END_ANSWERED),
                                                   end_of("GET", LABEL_1_ID, TaskEnd::TASK_END_ANSWERED)}));
}

TEST_F(SECTouchTaskEndTest, ANakAndATimeoutEachEndTheirTask) {
  this->queue_label_reads({LABEL_1_ID, LABEL_2_ID});
  this->send_next_task();
  this->uart_.push_rx(nak_frame());
  this->loop_times(2);
  this->age_task_past_watchdog();
  this->hub_.loop();

  EXPECT_EQ(this->ends_, (std::vector<std::string>{end_of("GET", LABEL_1_ID, TaskEnd::TASK_END_NAK),
                                                   end_of("GET", LABEL_2_ID, TaskEnd::TASK_END_TIMEOUT)}));
}

// A GET sent once more after a dropped reply has not ended yet; only its second malformed reply ends it.
TEST_F(SECTouchTaskEndTest, AMalformedGetEndsOnceAfterItsRetry) {
  this->queue_label_reads({LABEL_1_ID});
  this->send_next_task();
  this->panel_answers(data_frame_with_bad_crc(LABEL_1_ID, "3"));
  EXPECT_TRUE(this->ends_.empty());

  this->panel_answers(data_frame_with_bad_crc(LABEL_1_ID, "3"));
  EXPECT_EQ(this->ends_, std::vector<std::string>{end_of("GET", LABEL_1_ID, TaskEnd::TASK_END_MALFORMED)});
}

// Scan mode clears the queue: every discarded task, the one in flight first, ends as a timeout.
TEST_F(SECTouchTaskEndTest, ScanModeEndsEveryDiscardedTaskAsATimeout) {
  this->queue_label_reads({LABEL_1_ID, LABEL_2_ID, LABEL_3_ID});
  this->send_next_task();

  this->hub_.enter_scan_mode();

  EXPECT_EQ(this->ends_, (std::vector<std::string>{end_of("GET", LABEL_1_ID, TaskEnd::TASK_END_TIMEOUT),
                                                   end_of("GET", LABEL_2_ID, TaskEnd::TASK_END_TIMEOUT),
                                                   end_of("GET", LABEL_3_ID, TaskEnd::TASK_END_TIMEOUT)}));
}

// Leaving scan mode drops the task in flight: it ends as a timeout, like the tasks a scan's start drops.
TEST_F(SECTouchTaskEndTest, ScanExitEndsTheTaskInFlightAsATimeout) {
  this->hub_.enter_scan_mode();
  this->hub_.add_discovery_get_task(FAN_1_LEVEL_ID);
  this->send_next_task();

  this->hub_.exit_scan_mode();

  EXPECT_EQ(this->ends_, std::vector<std::string>{end_of("GET", FAN_1_LEVEL_ID, TaskEnd::TASK_END_TIMEOUT)});
}

// A latest-wins SET carries a ticket to its end, a new one when a newer value replaces it in place.
TEST_F(SECTouchTaskEndTest, AReplacedSetEndsWithItsNewTicket) {
  std::vector<uint32_t> tickets;
  this->hub_.register_task_end_listener([&tickets](const BaseTask &task, TaskEnd) { tickets.push_back(task.ticket); });
  const uint32_t first = this->hub_.add_set_task_latest_wins(TaskTargetType::LEVEL, FAN_1_LEVEL_ID, "5");
  const uint32_t second = this->hub_.add_set_task_latest_wins(TaskTargetType::LEVEL, FAN_1_LEVEL_ID, "6");
  this->send_next_task();

  this->uart_.push_rx(ack_frame());
  this->loop_times(2);

  EXPECT_NE(first, 0u);
  EXPECT_NE(second, first);
  EXPECT_EQ(tickets, std::vector<uint32_t>{second});
}

}  // namespace esphome::sec_touch::testing
