#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>

#include "sec_touch/time_program_live_model.h"

using esphome::sec_touch::AUTOMATIC_TIME_LEVEL;
using esphome::sec_touch::format_enabled_mask;
using esphome::sec_touch::LATE_CLOCK_READ_MS;
using esphome::sec_touch::LEVEL_NAMES;
using esphome::sec_touch::LevelName;
using esphome::sec_touch::LIVE_STATE_CONFLICT;
using esphome::sec_touch::LIVE_STATE_NONE;
using esphome::sec_touch::LIVE_STATE_NOT_RUNNING;
using esphome::sec_touch::LiveState;
using esphome::sec_touch::LiveStateModel;
using esphome::sec_touch::MS_PER_DAY;
using esphome::sec_touch::MS_PER_HOUR;
using esphome::sec_touch::MS_PER_MINUTE;
using esphome::sec_touch::MS_PER_S;
using esphome::sec_touch::next_change_timestamp;
using esphome::sec_touch::PanelClock;
using esphome::sec_touch::SECONDS_PER_MINUTE;
using esphome::sec_touch::SLEEP_LEVEL;
using esphome::sec_touch::SWITCH_TIME_COUNT;
using esphome::sec_touch::switch_time_bit;
using esphome::sec_touch::switch_time_name;
using esphome::sec_touch::time_program_enabled_id;
using esphome::sec_touch::time_program_level_id;
using esphome::sec_touch::time_program_slot;
using esphome::sec_touch::time_program_time_id;
using esphome::sec_touch::known_property_detail::level_name;

namespace {

constexpr uint32_t hours(uint32_t n) { return n * MS_PER_HOUR; }
constexpr uint32_t minutes(uint32_t n) { return n * MS_PER_MINUTE; }
constexpr uint32_t seconds(uint32_t n) { return n * MS_PER_S; }

/// An arbitrary millis() reading the panel clock is read at.
constexpr uint32_t READ_MS = 5000;

// ----- PanelClock -----

TEST(PanelClock, IsUnsetUntilAGoodRead) {
  PanelClock clock;
  EXPECT_FALSE(clock.is_set());
  EXPECT_FALSE(clock.take("24:00:00", READ_MS));
  EXPECT_FALSE(clock.is_set());
  EXPECT_TRUE(clock.take("10:00:00", READ_MS));
  EXPECT_TRUE(clock.is_set());
}

TEST(PanelClock, AdvancesWithMillisBetweenReads) {
  PanelClock clock;
  clock.take("10:00:00", READ_MS);

  EXPECT_EQ(clock.ms_of_day(READ_MS), hours(10));
  EXPECT_EQ(clock.ms_of_day(READ_MS + seconds(90) + 500), hours(10) + seconds(90) + 500);
}

TEST(PanelClock, WrapsAtMidnight) {
  PanelClock clock;
  clock.take("23:59:59", READ_MS);

  EXPECT_EQ(clock.ms_of_day(READ_MS + seconds(2)), seconds(1));
}

TEST(PanelClock, SurvivesTheMillisWrap) {
  constexpr uint32_t JUST_BEFORE_WRAP = UINT32_MAX - 999;
  PanelClock clock;
  clock.take("10:00:00", JUST_BEFORE_WRAP);

  EXPECT_EQ(clock.ms_of_day(JUST_BEFORE_WRAP + seconds(3)), hours(10) + seconds(3));
}

TEST(PanelClock, ReanchorsOnEachGoodRead) {
  PanelClock clock;
  clock.take("10:00:00", READ_MS);
  clock.take("10:00:30", READ_MS + seconds(20));

  EXPECT_EQ(clock.ms_of_day(READ_MS + seconds(20)), hours(10) + seconds(30));
}

// Spec "Live state" cases: a bad clock read keeps the old anchor.
TEST(PanelClock, AnUnexpectedReadKeepsTheOldAnchor) {
  PanelClock clock;
  clock.take("10:00:00", READ_MS);

  EXPECT_FALSE(clock.take("garbage", READ_MS + seconds(20)));
  EXPECT_EQ(clock.ms_of_day(READ_MS + seconds(20)), hours(10) + seconds(20));
}

// ----- LiveStateModel -----

constexpr int PAIR = 4;

/// The pair's level ID in Automatik Zeit, and in another mode.
const std::string RUNNING = std::to_string(AUTOMATIC_TIME_LEVEL);

std::string mask_text(uint8_t mask) { return std::string(format_enabled_mask(mask).data()); }
uint8_t bit(int switch_time) { return switch_time_bit(switch_time); }

std::string level_text(int level) { return std::to_string(level); }

/// Levels the switch times run in the tests, one per switch time so each is told apart.
constexpr int LEVEL_OF[SWITCH_TIME_COUNT + 1] = {-1, 1, 2, 3, 4, 5};
const char *running(int switch_time) { return level_name(LEVEL_OF[switch_time]); }

/// A pair's model and helpers that feed it one input each; nothing is read yet.
class LiveStateModelInputs : public ::testing::Test {
 protected:
  bool time(int switch_time, const std::string &raw) {
    return this->model_.take_block_value(*time_program_slot(time_program_time_id(PAIR, switch_time)), raw);
  }
  bool level(int switch_time, const std::string &raw) {
    return this->model_.take_block_value(*time_program_slot(time_program_level_id(PAIR, switch_time)), raw);
  }
  bool mask(const std::string &raw) {
    return this->model_.take_block_value(*time_program_slot(time_program_enabled_id(PAIR)), raw);
  }
  bool pair_level(const std::string &raw) { return this->model_.take_pair_level(raw); }
  bool clock(const std::string &raw) { return this->model_.take_clock(raw, READ_MS); }

  /// Switch time `switch_time` at 08:00 and LEVEL_OF its number.
  void switch_time(int switch_time) {
    this->time(switch_time, "08:00:00");
    this->level(switch_time, level_text(LEVEL_OF[switch_time]));
  }

  /// The mask, pair level and clock read once: these switch times enabled, running, the panel at `now`.
  void ready(uint8_t enabled, const std::string &now) {
    this->mask(mask_text(enabled));
    this->pair_level(RUNNING);
    this->clock(now);
  }

  bool has_state() const { return this->model_.compute(READ_MS).has_value(); }

  /// The state `after_ms` after the clock read.
  LiveState state(uint32_t after_ms = 0) {
    auto state = this->model_.compute(READ_MS + after_ms);
    EXPECT_TRUE(state.has_value());
    return state.value_or(LiveState{"", "", "", std::nullopt});
  }

  LiveStateModel model_{PAIR};
};

/// A pair's model fed one good value of every switch time's time and level.
class LiveStateModelTest : public LiveStateModelInputs {
 protected:
  /// Switch times 1-5 at these times, each at LEVEL_OF its number.
  void SetUp() override {
    const char *times[SWITCH_TIME_COUNT] = {"08:00:00", "12:00:00", "18:00:00", "21:00:00", "23:00:00"};
    for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
      this->time(n, times[n - 1]);
      this->level(n, level_text(LEVEL_OF[n]));
    }
  }
};

void expect_state(const LiveState &state, const char *in_effect, const char *running_level, const char *next_level,
                  std::optional<uint32_t> ms_to_next_change) {
  EXPECT_STREQ(state.in_effect, in_effect);
  EXPECT_STREQ(state.running_level, running_level);
  EXPECT_STREQ(state.next_level, next_level);
  EXPECT_EQ(state.ms_to_next_change, ms_to_next_change);
}

// Cases table, inputs rows: only the inputs the result uses are needed.
TEST_F(LiveStateModelInputs, NothingBeforeThePairLevelIsRead) {
  this->mask(mask_text(bit(1)));
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    this->switch_time(n);
  }
  this->clock("10:00:00");

  EXPECT_FALSE(this->has_state());
}

TEST_F(LiveStateModelInputs, NotRunningNeedsOnlyThePairLevel) {
  EXPECT_TRUE(this->pair_level(level_text(LEVEL_NAMES[0].level)));

  expect_state(this->state(), LIVE_STATE_NOT_RUNNING, LIVE_STATE_NOT_RUNNING, LIVE_STATE_NOT_RUNNING, std::nullopt);
}

TEST_F(LiveStateModelInputs, RunningNeedsTheClock) {
  this->mask(mask_text(bit(1)));
  this->switch_time(1);
  this->pair_level(RUNNING);
  EXPECT_FALSE(this->has_state()) << "the clock was never read";

  this->clock("10:00:00");
  expect_state(this->state(), switch_time_name(1), running(1), running(1), hours(22));
}

TEST_F(LiveStateModelInputs, RunningNeedsTheMask) {
  this->switch_time(1);
  this->pair_level(RUNNING);
  this->clock("10:00:00");
  EXPECT_FALSE(this->has_state()) << "the mask was never read";

  this->mask(mask_text(bit(1)));
  expect_state(this->state(), switch_time_name(1), running(1), running(1), hours(22));
}

TEST_F(LiveStateModelInputs, RunningNeedsTheTimeOfEachEnabledSwitchTime) {
  this->level(2, level_text(LEVEL_OF[2]));
  this->ready(bit(2), "10:00:00");
  EXPECT_FALSE(this->has_state()) << "switch time 2's time was never read";

  this->time(2, "08:00:00");
  expect_state(this->state(), switch_time_name(2), running(2), running(2), hours(22));
}

TEST_F(LiveStateModelInputs, RunningNeedsTheLevelOfEachEnabledSwitchTime) {
  this->time(2, "08:00:00");
  this->ready(bit(2), "10:00:00");
  EXPECT_FALSE(this->has_state()) << "switch time 2's level was never read";

  this->level(2, level_text(LEVEL_OF[2]));
  expect_state(this->state(), switch_time_name(2), running(2), running(2), hours(22));
}

TEST_F(LiveStateModelInputs, RunningNeedsNothingOfADisabledSwitchTime) {
  this->switch_time(1);
  this->ready(bit(1), "10:00:00");  // switch times 2-5 never read

  expect_state(this->state(), switch_time_name(1), running(1), running(1), hours(22));
}

TEST_F(LiveStateModelInputs, AnUnexpectedFirstReadOfADisabledSwitchTimeIsNotNeeded) {
  this->switch_time(1);
  this->ready(bit(1), "10:00:00");
  EXPECT_FALSE(this->time(2, "24:00:00"));
  EXPECT_FALSE(this->level(2, level_text(AUTOMATIC_TIME_LEVEL)));

  expect_state(this->state(), switch_time_name(1), running(1), running(1), hours(22));
}

TEST_F(LiveStateModelInputs, EnablingASwitchTimeNeverReadIsAsBoot) {
  this->switch_time(1);
  this->ready(bit(1), "10:00:00");
  ASSERT_TRUE(this->has_state());

  this->mask(mask_text(bit(1) | bit(2)));
  EXPECT_FALSE(this->has_state()) << "switch time 2 was never read";
}

// Cases table, not running: whenever the level ID isn't Automatik Zeit, whatever the program says.
TEST_F(LiveStateModelTest, NotRunningWheneverTheLevelIsNotAutomaticTime) {
  this->ready(bit(1) | bit(2), "10:00:00");
  for (const LevelName &entry : LEVEL_NAMES) {
    if (entry.level == AUTOMATIC_TIME_LEVEL) {
      continue;
    }
    SCOPED_TRACE(entry.name);
    EXPECT_TRUE(this->pair_level(level_text(entry.level)));
    expect_state(this->state(), LIVE_STATE_NOT_RUNNING, LIVE_STATE_NOT_RUNNING, LIVE_STATE_NOT_RUNNING, std::nullopt);
  }
}

TEST_F(LiveStateModelTest, NoneWhenNoSwitchTimeIsEnabled) {
  this->ready(0, "10:00:00");

  expect_state(this->state(), LIVE_STATE_NONE, LIVE_STATE_NONE, LIVE_STATE_NONE, std::nullopt);
}

TEST_F(LiveStateModelTest, NotRunningWinsOverNoneEnabled) {
  this->ready(0, "10:00:00");
  this->pair_level(level_text(LEVEL_NAMES[0].level));

  expect_state(this->state(), LIVE_STATE_NOT_RUNNING, LIVE_STATE_NOT_RUNNING, LIVE_STATE_NOT_RUNNING, std::nullopt);
}

// AC 9: exactly one enabled switch time is in effect all day; its next change is itself, 24 h later.
TEST_F(LiveStateModelTest, ExactlyOneEnabledSwitchTime) {
  this->ready(bit(2), "10:00:00");  // switch time 2 at 12:00

  expect_state(this->state(), switch_time_name(2), running(2), running(2), hours(2));
  expect_state(this->state(hours(4)), switch_time_name(2), running(2), running(2), hours(22));
}

TEST_F(LiveStateModelTest, ExactlyOneEnabledSwitchTimeAtThePanelTime) {
  this->ready(bit(2), "12:00:00");

  expect_state(this->state(), switch_time_name(2), running(2), running(2), MS_PER_DAY);
}

// AC 9: a switch time exactly at the panel time is in effect; the next change is strictly after it.
TEST_F(LiveStateModelTest, ASwitchTimeAtThePanelTimeIsInEffect) {
  this->ready(bit(1) | bit(2) | bit(3), "12:00:00");

  expect_state(this->state(), switch_time_name(2), running(2), running(3), hours(6));
}

TEST_F(LiveStateModelTest, OneSecondBeforeASwitchTimeTheEarlierOneIsInEffect) {
  this->ready(bit(1) | bit(2) | bit(3), "11:59:59");

  expect_state(this->state(), switch_time_name(1), running(1), running(2), seconds(1));
  expect_state(this->state(seconds(1)), switch_time_name(2), running(2), running(3), hours(6));
}

TEST_F(LiveStateModelTest, BeforeTodaysFirstYesterdaysLastIsInEffect) {
  this->ready(bit(1) | bit(2) | bit(3), "06:00:00");

  expect_state(this->state(), switch_time_name(3), running(3), running(1), hours(2));
}

TEST_F(LiveStateModelTest, AfterTodaysLastTheNextChangeIsTomorrowsFirst) {
  this->ready(bit(1) | bit(2) | bit(3), "20:00:00");

  expect_state(this->state(), switch_time_name(3), running(3), running(1), hours(12));
}

TEST_F(LiveStateModelTest, DisabledSwitchTimesAreIgnored) {
  this->ready(bit(1) | bit(3), "13:00:00");  // switch time 2 at 12:00 is off

  expect_state(this->state(), switch_time_name(1), running(1), running(3), hours(5));
}

TEST_F(LiveStateModelTest, TheClockAdvancesBetweenReads) {
  this->ready(bit(1) | bit(2), "11:00:00");

  expect_state(this->state(hours(2)), switch_time_name(2), running(2), running(1), hours(19));
}

// #139 item 2: a clock read arrives late, so it can pull the clock back across a change that just passed. A step back
// under LATE_CLOCK_READ_MS across a change is ignored: the change doesn't flip back and forward again.
TEST_F(LiveStateModelTest, ALateClockReadAcrossAChangeThatJustPassedIsIgnored) {
  this->ready(bit(1) | bit(2) | bit(3), "11:59:59");
  const uint32_t after = seconds(1) + 400;  // the clock shows 12:00:00.4: switch time 2 took effect
  ASSERT_STREQ(this->state(after).in_effect, switch_time_name(2));

  // 1.4 s back, across 12:00.
  EXPECT_TRUE(this->model_.take_clock("11:59:59", READ_MS + after));

  expect_state(this->state(after), switch_time_name(2), running(2), running(3), hours(6) - 400);
}

TEST_F(LiveStateModelTest, ALargerStepBackAcrossAChangeStillMovesTheClock) {
  this->ready(bit(1) | bit(2) | bit(3), "11:59:59");
  const uint32_t after = seconds(1) + 400;

  // 3.4 s back, across 12:00: more than a late read.
  static_assert(LATE_CLOCK_READ_MS < seconds(3) + 400);
  EXPECT_TRUE(this->model_.take_clock("11:59:57", READ_MS + after));

  EXPECT_STREQ(this->state(after).in_effect, switch_time_name(1));
}

TEST_F(LiveStateModelTest, ASmallStepBackWithNoChangeInBetweenMovesTheClock) {
  this->ready(bit(1) | bit(2) | bit(3), "10:00:00");
  const uint32_t after = seconds(1) + 400;

  EXPECT_TRUE(this->model_.take_clock("10:00:00", READ_MS + after));

  EXPECT_EQ(this->state(after).ms_to_next_change, hours(2));
}

TEST_F(LiveStateModelTest, AStepForwardAcrossAChangeMovesTheClock) {
  this->ready(bit(1) | bit(2) | bit(3), "11:59:59");

  EXPECT_TRUE(this->model_.take_clock("12:00:00", READ_MS + 500));

  expect_state(this->state(500), switch_time_name(2), running(2), running(3), hours(6));
}

// Cases table: two enabled switch times tied at the time in effect.
TEST_F(LiveStateModelTest, ATieAtTheTimeInEffectIsAConflict) {
  this->time(4, "08:00:00");  // same as switch time 1
  this->ready(bit(1) | bit(2) | bit(4), "09:00:00");

  expect_state(this->state(), LIVE_STATE_CONFLICT, LIVE_STATE_CONFLICT, running(2), hours(3));
}

TEST_F(LiveStateModelTest, ATieIsAConflictEvenWithTheSameLevel) {
  this->time(4, "08:00:00");
  this->level(4, level_text(LEVEL_OF[1]));
  this->ready(bit(1) | bit(2) | bit(4), "09:00:00");

  expect_state(this->state(), LIVE_STATE_CONFLICT, LIVE_STATE_CONFLICT, running(2), hours(3));
}

// Cases table: the next change is a tie.
TEST_F(LiveStateModelTest, ATieAtTheNextChangeIsAConflictOnlyForTheNextLevel) {
  this->time(4, "08:00:00");
  this->ready(bit(1) | bit(2) | bit(4), "07:00:00");

  expect_state(this->state(), switch_time_name(2), running(2), LIVE_STATE_CONFLICT, hours(1));
}

// Cases table: a tie at another time of day has no effect.
TEST_F(LiveStateModelTest, ATieAtAnotherTimeHasNoEffect) {
  this->time(4, "08:00:00");
  this->ready(bit(1) | bit(2) | bit(3) | bit(4), "13:00:00");

  expect_state(this->state(), switch_time_name(2), running(2), running(3), hours(5));
}

TEST_F(LiveStateModelTest, ATieOfDisabledSwitchTimesHasNoEffect) {
  this->time(4, "08:00:00");
  this->ready(bit(1) | bit(2), "09:00:00");  // switch time 4 is off

  expect_state(this->state(), switch_time_name(1), running(1), running(2), hours(3));
}

TEST_F(LiveStateModelTest, SwitchTimeSecondsCount) {
  this->time(2, "12:00:30");
  this->ready(bit(1) | bit(2), "12:00:10");

  expect_state(this->state(), switch_time_name(1), running(1), running(2), seconds(20));
}

// AC 9 and the cases table: a failed or unexpected input after a good read keeps its last good value.
TEST_F(LiveStateModelTest, AnUnexpectedLevelKeepsTheLastGoodOne) {
  this->ready(bit(2), "13:00:00");

  EXPECT_FALSE(this->level(2, level_text(AUTOMATIC_TIME_LEVEL)));
  expect_state(this->state(), switch_time_name(2), running(2), running(2), hours(23));
}

TEST_F(LiveStateModelTest, AnUnexpectedTimeKeepsTheLastGoodOne) {
  this->ready(bit(2), "13:00:00");

  EXPECT_FALSE(this->time(2, "24:00:00"));
  expect_state(this->state(), switch_time_name(2), running(2), running(2), hours(23));
}

TEST_F(LiveStateModelTest, AnUnexpectedMaskKeepsTheLastGoodOne) {
  this->ready(bit(2), "13:00:00");

  EXPECT_FALSE(this->mask(mask_text(switch_time_bit(SWITCH_TIME_COUNT + 1))));
  expect_state(this->state(), switch_time_name(2), running(2), running(2), hours(23));
}

TEST_F(LiveStateModelTest, AnUnexpectedPairLevelKeepsTheLastGoodOne) {
  this->ready(bit(2), "13:00:00");

  EXPECT_FALSE(this->pair_level("abc"));
  EXPECT_FALSE(this->pair_level(level_text(SLEEP_LEVEL + 1)));
  expect_state(this->state(), switch_time_name(2), running(2), running(2), hours(23));
}

TEST_F(LiveStateModelTest, AGoodValueReplacesTheLastGoodOne) {
  this->ready(bit(2), "13:00:00");

  EXPECT_TRUE(this->level(2, level_text(LEVEL_OF[5])));
  EXPECT_TRUE(this->mask(mask_text(bit(2) | bit(3))));
  expect_state(this->state(), switch_time_name(2), running(5), running(3), hours(5));
}

// AC 9 and the cases table: a needed input that never read well yet is as boot.
TEST_F(LiveStateModelInputs, AnUnexpectedFirstMaskIsAsBoot) {
  this->switch_time(1);
  this->pair_level(RUNNING);
  this->clock("10:00:00");

  EXPECT_FALSE(this->mask("zz"));
  EXPECT_FALSE(this->has_state());

  EXPECT_TRUE(this->mask(mask_text(bit(1))));
  EXPECT_TRUE(this->has_state());
}

TEST_F(LiveStateModelInputs, AnUnexpectedFirstClockIsAsBoot) {
  this->mask(mask_text(bit(1)));
  this->switch_time(1);
  this->pair_level(RUNNING);

  EXPECT_FALSE(this->clock("25:00:00"));
  EXPECT_FALSE(this->has_state());
}

TEST_F(LiveStateModelInputs, AnUnexpectedFirstLevelOfAnEnabledSwitchTimeIsAsBoot) {
  this->time(1, "08:00:00");
  this->ready(bit(1), "10:00:00");

  EXPECT_FALSE(this->level(1, level_text(AUTOMATIC_TIME_LEVEL)));
  EXPECT_FALSE(this->has_state());
}

TEST(LiveStateModelBoot, AnotherPairsValueIsNotAnInput) {
  constexpr int OTHER_PAIR = PAIR + 1;
  LiveStateModel model{PAIR};
  EXPECT_FALSE(model.take_block_value(*time_program_slot(time_program_enabled_id(OTHER_PAIR)), mask_text(bit(1))));
}

// ----- next_change_timestamp -----

/// A whole minute since the epoch.
constexpr int64_t NOW_S = 1800000000;
static_assert(NOW_S % SECONDS_PER_MINUTE == 0, "NOW_S is a whole minute");

TEST(NextChangeTimestamp, IsNowPlusTheWaitRoundedToTheMinute) {
  EXPECT_EQ(next_change_timestamp(NOW_S, hours(2)), NOW_S + hours(2) / MS_PER_S);
  EXPECT_EQ(next_change_timestamp(NOW_S, hours(2) + seconds(29) + 999), NOW_S + hours(2) / MS_PER_S);
  EXPECT_EQ(next_change_timestamp(NOW_S, hours(2) + seconds(30)), NOW_S + (hours(2) + minutes(1)) / MS_PER_S);
  EXPECT_EQ(next_change_timestamp(NOW_S + 10, hours(2)), NOW_S + hours(2) / MS_PER_S);
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
