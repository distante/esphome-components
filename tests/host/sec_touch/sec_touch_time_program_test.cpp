#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
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

/// What a select without state reads as, so a comparison fails.
constexpr size_t NO_INDEX = SIZE_MAX;

std::string text(const char *value) { return std::string(value); }

std::string event_name(WriteFailure failure) { return write_failure_name(failure); }

/// Fan pair PAIR with entities for switch times FIRST_SWITCH_TIME and SWITCH_TIME and a write_failed event, driven
/// through the real entity classes and the real hub, with a fake bus.
class TimeProgramTest : public ::testing::Test {
 protected:
  void SetUp() override {
    this->hub_.set_uart_parent(&this->uart_);
    this->hub_.set_inter_task_delay(0);
    // Host millis() is the real clock: start both polls' intervals now, as setup() would.
    this->hub_.last_time_programs_update_ms_ = millis();
    this->hub_.last_global_settings_update_ms_ = millis();
    this->write_failed_.add_on_event_callback(
        [this](StringRef type) { this->events_.push_back(std::string(type.c_str())); });
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
    this->hub_.loop_times(3);
    return this->uart_.tx;
  }

  std::vector<uint8_t> panel_answers_get(int property_id, const std::string &value) {
    return this->panel_replies(concat(ack_frame(), data_frame(property_id, value)));
  }

  /// The task in flight gets no answer. Returns the next task the hub sent.
  std::vector<uint8_t> panel_stays_silent() {
    this->uart_.tx.clear();
    this->hub_.age_task_past_watchdog();
    this->hub_.loop_times(2);
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

  this->hub_.last_time_programs_update_ms_ = millis() - (TEST_TIME_PROGRAMS_INTERVAL_MS + 1);
  this->hub_.loop();  // an idle loop queues the due poll

  EXPECT_EQ(this->drain(), one_poll());
}

// millis() is 32 bits and wraps: a poll that last ran just before the wrap is due after its interval, not at once.
// The longest interval makes this hold at any uptime of the host clock.
TEST_F(TimeProgramTest, APollThatRanBeforeTheMillisWrapIsDueOnlyAfterItsInterval) {
  this->hub_.set_time_programs_update_interval_ms(std::numeric_limits<uint32_t>::max());
  this->hub_.setup();
  this->drain();

  // Ran when millis() last read its highest value; millis() has since wrapped to its current value.
  this->hub_.last_time_programs_update_ms_ = std::numeric_limits<uint32_t>::max();
  this->hub_.loop();  // an idle loop queues the poll only when due

  EXPECT_TRUE(this->send_next_task().empty());
}

// AC 5: the global-settings poll sends its own IDs only.
// sec-touch: a task creation that refuses an ID returns null. A poll ID it refuses is skipped, not queued as null.
TEST_F(TimeProgramTest, APollIdWithNoTaskIsSkipped) {
  constexpr int NOT_A_TIME_PROGRAM_ID = FAN_LEVEL_IDS[0];
  this->hub_.time_program_polls_[PAIR - 1].add(NOT_A_TIME_PROGRAM_ID);

  this->program_.refresh();

  const auto &queue = this->hub_.data_task_queue;
  EXPECT_TRUE(std::none_of(queue.begin(), queue.end(), [](const auto &task) { return task == nullptr; }));
  EXPECT_EQ(queue.size(), one_poll().size());
}

TEST_F(TimeProgramTest, TheGlobalSettingsPollSendsNoTimeProgramId) {
  this->hub_.register_global_setting_string_listener(CLOCK_PROPERTY_ID, [](int, const std::string &) {});
  this->hub_.setup();
  this->drain();

  this->hub_.last_global_settings_update_ms_ = millis() - (TEST_GLOBAL_SETTINGS_INTERVAL_MS + 1);
  this->hub_.loop();  // an idle loop queues the due poll

  EXPECT_EQ(this->drain(), std::vector<std::vector<uint8_t>>{get_frame(CLOCK_PROPERTY_ID)});
}

// AC 5: one press queues one poll of the pair and leaves the schedule as it was.
TEST_F(TimeProgramTest, RefreshQueuesOnePollAndKeepsTheSchedule) {
  this->hub_.setup();
  this->drain();
  // Halfway through the interval, so a refresh that restarted it would show.
  this->hub_.last_time_programs_update_ms_ = millis() - TEST_TIME_PROGRAMS_INTERVAL_MS / 2;
  const uint32_t scheduled = this->hub_.last_time_programs_update_ms_;

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

// A call that gives only the minute (ESPHome allows it) takes the hour from the entity's state; without a state there
// is no hour to keep, so nothing is written.
TEST_F(TimeProgramTest, AMinuteOnlyCallWithoutStateWritesNothing) {
  this->time_.make_call().set_minute(30).perform();

  EXPECT_TRUE(this->send_next_task().empty());
  EXPECT_FALSE(this->time_.has_state());
}

TEST_F(TimeProgramTest, AnHourOnlyCallKeepsTheMinuteShown) {
  this->panel_replies(data_frame(TIME_ID, "08:30:00"));

  this->time_.make_call().set_hour(9).perform();

  EXPECT_EQ(this->send_next_task(), set_frame(TIME_ID, "09:30:00"));
}

// A time outside the day is never sent.
TEST_F(TimeProgramTest, ATimeOutsideTheDayIsNotWritten) {
  this->program_.write_time(SWITCH_TIME, HOURS_PER_DAY, 0);
  this->program_.write_time(SWITCH_TIME, 0, MINUTES_PER_HOUR);

  EXPECT_TRUE(this->send_next_task().empty());
}

// A level outside the switch time levels is never sent.
TEST_F(TimeProgramTest, ALevelOutsideTheSwitchTimeLevelsIsNotWritten) {
  this->program_.write_level(SWITCH_TIME, OFF_LEVEL - 1);
  this->program_.write_level(SWITCH_TIME, MAX_SWITCH_TIME_LEVEL + 1);

  EXPECT_TRUE(this->send_next_task().empty());
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

// AC 6 and 7: a NAK is reported, never retried; the read-back GET shows the truth and is no second report.
TEST_F(TimeProgramTest, ARefusedSetIsReportedAndNotRetried) {
  this->level_.make_call().set_index(4).perform();
  this->send_next_task();

  EXPECT_EQ(this->panel_replies(nak_frame()), get_frame(LEVEL_ID));
  EXPECT_EQ(this->events_, std::vector<std::string>{event_name(WriteFailure::REFUSED)});

  this->panel_answers_get(LEVEL_ID, "2");
  EXPECT_EQ(this->level_.active_index().value_or(NO_INDEX), (size_t) 2);
  EXPECT_EQ(this->events_, std::vector<std::string>{event_name(WriteFailure::REFUSED)});
}

TEST_F(TimeProgramTest, AnUnansweredSetIsATimeout) {
  this->time_.make_call().set_time(6, 0, 0).perform();
  this->send_next_task();

  EXPECT_EQ(this->panel_stays_silent(), get_frame(TIME_ID));
  EXPECT_EQ(this->events_, std::vector<std::string>{event_name(WriteFailure::TIMEOUT)});

  this->panel_answers_get(TIME_ID, "05:00:00");
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

// Decisions: only the switch write's own fresh mask GET decides mask_unreadable. A poll's mask GET in flight when the
// switch is written that fails leaves the bits to the write's own GET.
TEST_F(TimeProgramTest, AFailedPollMaskGetLeavesTheWriteToItsOwnGet) {
  this->program_.refresh();
  EXPECT_EQ(this->send_next_task(), get_frame(FIRST_TIME_ID));
  this->panel_answers_get(FIRST_TIME_ID, "06:00:00");
  EXPECT_EQ(this->panel_answers_get(FIRST_LEVEL_ID, "2"), concat(ack_frame(), get_frame(MASK_ID)));
  this->enabled_.turn_on();

  EXPECT_EQ(this->panel_stays_silent(), get_frame(MASK_ID));
  EXPECT_TRUE(this->events_.empty());

  EXPECT_EQ(this->panel_answers_get(MASK_ID, mask_text(0)),
            concat(ack_frame(), set_frame(MASK_ID, mask_text(switch_time_bit(SWITCH_TIME)))));
  EXPECT_TRUE(this->enabled_.state);
}

// Only the switch write's own fresh mask GET gives the mask it writes: a poll's answer of the mask, in flight when the
// switch is written, is older than the write.
TEST_F(TimeProgramTest, APollMaskAnswerIsNotTheWritesFreshMask) {
  this->program_.refresh();
  EXPECT_EQ(this->send_next_task(), get_frame(FIRST_TIME_ID));
  this->panel_answers_get(FIRST_TIME_ID, "06:00:00");
  EXPECT_EQ(this->panel_answers_get(FIRST_LEVEL_ID, "2"), concat(ack_frame(), get_frame(MASK_ID)));
  this->enabled_.turn_on();

  EXPECT_EQ(this->panel_answers_get(MASK_ID, mask_text(switch_time_bit(FIRST_SWITCH_TIME))),
            concat(ack_frame(), get_frame(MASK_ID)));
  EXPECT_EQ(this->panel_answers_get(MASK_ID, mask_text(0)),
            concat(ack_frame(), set_frame(MASK_ID, mask_text(switch_time_bit(SWITCH_TIME)))));
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

// Decisions: scan mode drops the queued SET; it ends as a timeout and gets no read-back.
TEST_F(TimeProgramTest, ScanModeTimesOutAQueuedSet) {
  this->level_.make_call().set_index(4).perform();

  this->hub_.enter_scan_mode();

  EXPECT_EQ(this->events_, std::vector<std::string>{event_name(WriteFailure::TIMEOUT)});
  EXPECT_TRUE(this->send_next_task().empty());
}

// Leaving scan mode drops a switch write's fresh mask GET in flight: mask_unreadable, and the next switch write reads
// the mask again once the watchdog frees the line.
TEST_F(TimeProgramTest, AScanExitEndsTheMaskGetInFlight) {
  this->hub_.enter_scan_mode();
  this->enabled_.turn_on();
  ASSERT_EQ(this->send_next_task(), get_frame(MASK_ID));

  this->hub_.exit_scan_mode();
  EXPECT_EQ(this->events_, std::vector<std::string>{event_name(WriteFailure::MASK_UNREADABLE)});

  this->enabled_.turn_off();
  EXPECT_EQ(this->panel_stays_silent(), get_frame(MASK_ID));
}

// Writes stay allowed during a scan: a SET it doesn't drop gets its read-back GET.
TEST_F(TimeProgramTest, ASetAckedDuringAScanGetsItsReadBack) {
  this->hub_.enter_scan_mode();
  this->level_.make_call().set_index(4).perform();
  ASSERT_EQ(this->send_next_task(), set_frame(LEVEL_ID, text(format_switch_time_level(4).data())));

  EXPECT_EQ(this->panel_replies(ack_frame()), get_frame(LEVEL_ID));
}

// A switch write whose fresh mask GET can't be queued writes nothing and fires mask_unreadable.
TEST_F(TimeProgramTest, AMaskGetThatCantBeQueuedFailsTheSwitchWrite) {
  this->hub_.unqueueable_priority_gets.insert(MASK_ID);

  this->enabled_.turn_on();

  EXPECT_EQ(this->events_, std::vector<std::string>{event_name(WriteFailure::MASK_UNREADABLE)});
  EXPECT_TRUE(this->send_next_task().empty());
}

// A switch write that writes nothing shows the last good mask again, not the state it never wrote.
TEST_F(TimeProgramTest, AFailedSwitchWriteShowsTheLastGoodMask) {
  this->panel_replies(data_frame(MASK_ID, mask_text(0)));
  ASSERT_FALSE(this->enabled_.state);
  this->hub_.unqueueable_priority_gets.insert(MASK_ID);

  this->enabled_.turn_on();

  EXPECT_FALSE(this->enabled_.state);
}

}  // namespace esphome::sec_touch::testing
