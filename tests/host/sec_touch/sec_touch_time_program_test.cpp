#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "common.h"
#include "esphome/components/sec_touch/time_program.h"
#include "esphome/components/sec_touch/time_program_codec.h"
#include "esphome/components/sec_touch/time_program_ids.h"
#include "esphome/components/sec_touch/time_program_write.h"

namespace esphome::sec_touch::testing {

namespace {

/// The listed pair, and the switch times its entities exist for.
constexpr int PAIR = 2;
constexpr int SWITCH_TIME = 3;
constexpr int FIRST_SWITCH_TIME = 1;

const int MASK_ID = time_program_enabled_id(PAIR);
const int TIME_ID = time_program_time_id(PAIR, SWITCH_TIME);
const int LEVEL_ID = time_program_level_id(PAIR, SWITCH_TIME);
const int FIRST_TIME_ID = time_program_time_id(PAIR, FIRST_SWITCH_TIME);
const int FIRST_LEVEL_ID = time_program_level_id(PAIR, FIRST_SWITCH_TIME);

constexpr uint32_t INTERVAL_MS = 300000;
constexpr uint32_t GLOBAL_INTERVAL_MS = 30000;
/// What a select without state reads as, so a comparison fails.
constexpr size_t NO_INDEX = SIZE_MAX;

std::string text(const char *value) { return std::string(value); }

std::string event_name(WriteFailure failure) { return write_failure_name(failure); }

std::string mask_text(uint8_t mask) { return text(format_enabled_mask(mask).data()); }

std::vector<uint8_t> concat(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
  std::vector<uint8_t> out = a;
  out.insert(out.end(), b.begin(), b.end());
  return out;
}

/// Fan pair PAIR with entities for switch times FIRST_SWITCH_TIME and SWITCH_TIME and a write_failed event, driven
/// through the real entity classes and the real hub, with a fake bus.
class TimeProgramTest : public ::testing::Test {
 protected:
  void SetUp() override {
    this->hub_.set_uart_parent(&this->uart_);
    this->hub_.set_inter_task_delay(0);
    this->hub_.set_time_programs_update_interval_ms(INTERVAL_MS);
    this->hub_.set_global_settings_update_interval_ms(GLOBAL_INTERVAL_MS);
    // Host millis() is the real clock: start both polls' intervals now, as setup() would.
    this->hub_.last_time_programs_update_ms_ = millis();
    this->hub_.last_global_settings_update_ms_ = millis();
    this->write_failed_.add_on_event_callback(
        [this](StringRef type) { this->events_.push_back(std::string(type.c_str())); });
  }

  void loop_times(int n) {
    for (int i = 0; i < n; i++) {
      this->hub_.loop();
    }
  }

  /// Sends the first queued task. Returns what went out: that request.
  std::vector<uint8_t> send_next_task() {
    this->uart_.tx.clear();
    this->hub_.loop();
    return this->uart_.tx;
  }

  /// The panel answers the task in flight with `reply` (an ACK, a NAK, or a GET's ACK and data). Returns what the hub
  /// sent meanwhile: the ACK of a data frame, then the next task.
  std::vector<uint8_t> panel_replies(const std::vector<uint8_t> &reply) {
    this->uart_.tx.clear();
    this->uart_.push_rx(reply);
    this->loop_times(3);
    return this->uart_.tx;
  }

  std::vector<uint8_t> panel_answers_get(int property_id, const std::string &value) {
    return this->panel_replies(concat(ack_frame(), data_frame(property_id, value)));
  }

  /// The task in flight gets no answer. Returns the next task the hub sent.
  std::vector<uint8_t> panel_stays_silent() {
    this->uart_.tx.clear();
    this->hub_.task_start_time_ = millis() - (TASK_TIMEOUT_MS + 1);
    this->loop_times(2);
    return this->uart_.tx;
  }

  /// Sends every queued task, each left unanswered. Returns each request that went out, in order.
  std::vector<std::vector<uint8_t>> drain() {
    std::vector<std::vector<uint8_t>> sent;
    std::vector<uint8_t> next = this->send_next_task();
    while (!next.empty()) {
      sent.push_back(next);
      next = this->panel_stays_silent();
    }
    return sent;
  }

  FakeUART uart_;
  TestableSECTouch hub_;
  TimeProgram program_{&this->hub_, PAIR};
  TimeProgramTime first_time_{&this->program_, FIRST_SWITCH_TIME};
  TimeProgramLevelSelect first_level_{&this->program_, FIRST_SWITCH_TIME};
  TimeProgramEnabledSwitch first_enabled_{&this->program_, FIRST_SWITCH_TIME};
  TimeProgramTime time_{&this->program_, SWITCH_TIME};
  TimeProgramLevelSelect level_{&this->program_, SWITCH_TIME};
  TimeProgramEnabledSwitch enabled_{&this->program_, SWITCH_TIME};
  TimeProgramWriteFailedEvent write_failed_{&this->program_};
  std::vector<std::string> events_;
};

/// One poll of PAIR: the IDs of the listed switch times and the mask, in the order the entities attached.
std::vector<std::vector<uint8_t>> one_poll() {
  return {get_frame(FIRST_TIME_ID), get_frame(FIRST_LEVEL_ID), get_frame(MASK_ID), get_frame(TIME_ID),
          get_frame(LEVEL_ID)};
}

}  // namespace

// AC 5: the first poll right after boot; only the listed pair, and only its listed switch times and mask.
TEST_F(TimeProgramTest, BootPollsTheListedIdsOfTheListedPair) {
  this->hub_.setup();

  EXPECT_EQ(this->drain(), one_poll());
}

TEST_F(TimeProgramTest, TheNextPollComesAfterItsInterval) {
  this->hub_.setup();
  this->drain();
  EXPECT_TRUE(this->send_next_task().empty());

  this->hub_.last_time_programs_update_ms_ = millis() - (INTERVAL_MS + 1);
  this->hub_.loop();  // an idle loop queues the due poll

  EXPECT_EQ(this->drain(), one_poll());
}

// AC 5: the global-settings poll sends its own IDs only.
TEST_F(TimeProgramTest, TheGlobalSettingsPollSendsNoTimeProgramId) {
  this->hub_.register_global_setting_string_listener(CLOCK_PROPERTY_ID, [](int, const std::string &) {});
  this->hub_.setup();
  this->drain();

  this->hub_.last_global_settings_update_ms_ = millis() - (GLOBAL_INTERVAL_MS + 1);
  this->hub_.loop();  // an idle loop queues the due poll

  EXPECT_EQ(this->drain(), std::vector<std::vector<uint8_t>>{get_frame(CLOCK_PROPERTY_ID)});
}

// AC 5: one press queues one poll of the pair and leaves the schedule as it was.
TEST_F(TimeProgramTest, RefreshQueuesOnePollAndKeepsTheSchedule) {
  this->hub_.setup();
  this->drain();
  // Halfway through the interval, so a refresh that restarted it would show.
  this->hub_.last_time_programs_update_ms_ = millis() - INTERVAL_MS / 2;
  const unsigned long scheduled = this->hub_.last_time_programs_update_ms_;

  this->program_.refresh();

  EXPECT_EQ(this->drain(), one_poll());
  EXPECT_EQ(this->hub_.last_time_programs_update_ms_, scheduled);
}

// AC 6 and 8: published at once; written as HH:MM:00, the seconds dropped.
TEST_F(TimeProgramTest, ATimeWriteIsPublishedAtOnceAndSentWithZeroSeconds) {
  this->time_.make_call().set_time(8, 30, 45).perform();

  EXPECT_TRUE(this->time_.has_state());
  EXPECT_EQ(this->time_.hour, 8);
  EXPECT_EQ(this->time_.minute, 30);
  EXPECT_EQ(this->time_.second, 0);
  EXPECT_EQ(this->send_next_task(), set_frame(TIME_ID, "08:30:00"));
}

// AC 6: one queued SET per ID, the newer value in place; after it ends, one GET of that ID, whose value is shown.
TEST_F(TimeProgramTest, ANewerWriteReplacesTheQueuedSetAndItsReadBackIsPublished) {
  this->level_.make_call().set_index(4).perform();
  this->level_.make_call().set_index(6).perform();

  EXPECT_EQ(this->send_next_task(), set_frame(LEVEL_ID, "6"));
  EXPECT_EQ(this->panel_replies(ack_frame()), get_frame(LEVEL_ID));
  EXPECT_EQ(this->panel_answers_get(LEVEL_ID, "6"), ack_frame());
  EXPECT_EQ(this->level_.active_index().value_or(NO_INDEX), (size_t) 6);
  EXPECT_TRUE(this->events_.empty());
}

// AC 6 and 7: a NAK is reported, never retried; the read-back GET shows the truth.
TEST_F(TimeProgramTest, ARefusedSetIsReportedAndNotRetried) {
  this->level_.make_call().set_index(4).perform();
  this->send_next_task();

  EXPECT_EQ(this->panel_replies(nak_frame()), get_frame(LEVEL_ID));
  EXPECT_EQ(this->events_, std::vector<std::string>{event_name(WriteFailure::REFUSED)});

  this->panel_answers_get(LEVEL_ID, "2");
  EXPECT_EQ(this->level_.active_index().value_or(NO_INDEX), (size_t) 2);
}

TEST_F(TimeProgramTest, AnUnansweredSetIsATimeout) {
  this->time_.make_call().set_time(6, 0, 0).perform();
  this->send_next_task();

  EXPECT_EQ(this->panel_stays_silent(), get_frame(TIME_ID));
  EXPECT_EQ(this->events_, std::vector<std::string>{event_name(WriteFailure::TIMEOUT)});
}

TEST_F(TimeProgramTest, AReadBackThatDiffersIsNotKept) {
  this->level_.make_call().set_index(4).perform();
  this->send_next_task();
  this->panel_replies(ack_frame());

  this->panel_answers_get(LEVEL_ID, "2");

  EXPECT_EQ(this->events_, std::vector<std::string>{event_name(WriteFailure::NOT_KEPT)});
  EXPECT_EQ(this->level_.active_index().value_or(NO_INDEX), (size_t) 2);
}

// Writing: seconds are ignored when comparing times.
TEST_F(TimeProgramTest, AReadBackTimeWithOtherSecondsIsKept) {
  this->time_.make_call().set_time(8, 30, 0).perform();
  this->send_next_task();
  this->panel_replies(ack_frame());

  this->panel_answers_get(TIME_ID, "08:30:15");

  EXPECT_TRUE(this->events_.empty());
}

// Decisions: a corrupted ACK fires nothing for the SET; the read-back GET decides.
TEST_F(TimeProgramTest, ACorruptedAckLeavesItToTheReadBack) {
  this->level_.make_call().set_index(4).perform();
  this->send_next_task();

  EXPECT_EQ(this->panel_replies(corrupted_ack()), get_frame(LEVEL_ID));
  EXPECT_TRUE(this->events_.empty());

  this->panel_answers_get(LEVEL_ID, "2");
  EXPECT_EQ(this->events_, std::vector<std::string>{event_name(WriteFailure::NOT_KEPT)});
}

// A poll answer older than a queued SET would flip the entity back.
TEST_F(TimeProgramTest, AnAnswerWhileASetIsQueuedLeavesTheEntityAsWritten) {
  this->level_.make_call().set_index(4).perform();

  this->panel_replies(data_frame(LEVEL_ID, "2"));

  EXPECT_EQ(this->level_.active_index().value_or(NO_INDEX), (size_t) 4);
}

// AC 7: GET the mask fresh, change only the requested bit, SET it, then read it back.
TEST_F(TimeProgramTest, ASwitchWriteGetsTheMaskThenSetsOnlyItsBit) {
  const uint8_t fresh = switch_time_bit(FIRST_SWITCH_TIME) | switch_time_bit(SWITCH_TIME + 1);
  this->enabled_.turn_on();
  EXPECT_TRUE(this->enabled_.state);

  EXPECT_EQ(this->send_next_task(), get_frame(MASK_ID));
  EXPECT_EQ(this->panel_answers_get(MASK_ID, mask_text(fresh)),
            concat(ack_frame(), set_frame(MASK_ID, mask_text(fresh | switch_time_bit(SWITCH_TIME)))));
  EXPECT_EQ(this->panel_replies(ack_frame()), get_frame(MASK_ID));
  EXPECT_TRUE(this->enabled_.state);
  EXPECT_TRUE(this->first_enabled_.state);
}

// Writing: requests merge until the mask is written; per bit, the later one wins.
TEST_F(TimeProgramTest, SwitchRequestsMergeIntoOneMaskWrite) {
  const uint8_t fresh = switch_time_bit(FIRST_SWITCH_TIME);
  this->enabled_.turn_on();
  this->first_enabled_.turn_on();
  this->first_enabled_.turn_off();

  EXPECT_EQ(this->send_next_task(), get_frame(MASK_ID));
  EXPECT_EQ(this->panel_answers_get(MASK_ID, mask_text(fresh)),
            concat(ack_frame(), set_frame(MASK_ID, mask_text(switch_time_bit(SWITCH_TIME)))));
}

// AC 7: a failed mask GET writes nothing, fires mask_unreadable and shows the last good mask again.
TEST_F(TimeProgramTest, AFailedMaskGetWritesNothing) {
  this->panel_replies(data_frame(MASK_ID, mask_text(switch_time_bit(FIRST_SWITCH_TIME))));
  ASSERT_FALSE(this->enabled_.state);
  this->enabled_.turn_on();
  EXPECT_EQ(this->send_next_task(), get_frame(MASK_ID));

  EXPECT_TRUE(this->panel_stays_silent().empty());

  EXPECT_EQ(this->events_, std::vector<std::string>{event_name(WriteFailure::MASK_UNREADABLE)});
  EXPECT_FALSE(this->enabled_.state);
  EXPECT_TRUE(this->first_enabled_.state);
}

// Entities: an unexpected mask leaves the switches as they were and writes nothing.
TEST_F(TimeProgramTest, AnUnexpectedFreshMaskWritesNothing) {
  this->enabled_.turn_on();
  this->send_next_task();

  EXPECT_EQ(this->panel_answers_get(MASK_ID, "20"), ack_frame());
  EXPECT_EQ(this->events_, std::vector<std::string>{event_name(WriteFailure::MASK_UNREADABLE)});
}

// AC 8: an unexpected level leaves the select with no state, and nothing is written.
TEST_F(TimeProgramTest, AnUnexpectedLevelLeavesTheSelectWithoutState) {
  this->panel_replies(data_frame(LEVEL_ID, "2"));
  ASSERT_TRUE(this->level_.has_state());

  EXPECT_EQ(this->panel_replies(data_frame(LEVEL_ID, std::to_string(AUTOMATIC_TIME_LEVEL))), ack_frame());

  EXPECT_FALSE(this->level_.has_state());
  EXPECT_TRUE(this->send_next_task().empty());
}

TEST_F(TimeProgramTest, AnUnexpectedTimeLeavesTheTimeWithoutState) {
  this->panel_replies(data_frame(TIME_ID, "08:30:00"));
  ASSERT_TRUE(this->time_.has_state());

  this->panel_replies(data_frame(TIME_ID, "24:00:00"));

  EXPECT_FALSE(this->time_.has_state());
}

// Decisions: scan mode drops the queued SET; it ends as a timeout, and no read-back goes out in scan mode.
TEST_F(TimeProgramTest, ScanModeTimesOutAQueuedSet) {
  this->level_.make_call().set_index(4).perform();

  this->hub_.enter_scan_mode();

  EXPECT_EQ(this->events_, std::vector<std::string>{event_name(WriteFailure::TIMEOUT)});
  EXPECT_TRUE(this->send_next_task().empty());
}

}  // namespace esphome::sec_touch::testing
