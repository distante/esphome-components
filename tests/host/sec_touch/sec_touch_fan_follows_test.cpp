#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "common.h"
#include "live_state_inputs.h"
#include "esphome/components/sec_touch/fan/sec_touch_fan.h"
#include "esphome/components/sec_touch/fan_level.h"
#include "esphome/components/sec_touch/time_program_ids.h"
#include "esphome/components/sec_touch/time_program_live_state.h"
#include "esphome/core/hal.h"

// .specs/time-programs.md, "Live state": in Automatik Zeit a following fan shows its pair's running level.

namespace esphome::sec_touch::testing {

namespace {

const int LABEL_ID = FAN_LABEL_IDS[PAIR - 1];
/// A normal level the tests feed in: a running level, the pair's level or a speed HA sets.
constexpr int SOME_NORMAL_LEVEL = 3;

/// What the fan showed at one publish.
struct Shown {
  bool on;
  int speed;
  std::string preset;
};

/// Pair PAIR's fan and live state.
class FanFollowsTest : public LiveStateInputsTest {
 protected:
  void SetUp() override {
    LiveStateInputsTest::SetUp();
    this->fan_.setup();
    this->fan_.add_on_state_callback([this]() {
      this->shown_.push_back(
          Shown{this->fan_.state, this->fan_.speed, std::string(this->fan_.get_preset_mode().c_str())});
    });
  }

  void follow() { this->fan_.follow_time_program(&this->live_); }

  /// The fan's level poll, answered with `level`.
  void panel_answers_pair_level(int level) {
    this->hub_.add_recursive_tasks_to_get_queue();
    this->send_next_task();
    this->panel_answers_get(PAIR_LEVEL_ID, std::to_string(level));
  }

  void reach_the_next_change() {
    delay(PAST_THE_NEXT_CHANGE_MS);
    this->live_.loop();
  }

  void expect_shown(bool on, int speed, const char *preset) {
    EXPECT_EQ(this->fan_.state, on);
    EXPECT_EQ(this->fan_.speed, speed);
    EXPECT_EQ(std::string(this->fan_.get_preset_mode().c_str()), preset);
  }

  /// Every publish so far, and the fan now, show it on at `speed` in Normal.
  void expect_every_publish_on_at(int speed) {
    for (const Shown &shown : this->shown_) {
      EXPECT_TRUE(shown.on);
      EXPECT_EQ(shown.speed, speed);
      EXPECT_EQ(shown.preset, NORMAL_PRESET_NAME);
    }
    this->expect_shown(true, speed, NORMAL_PRESET_NAME);
  }

  TimeProgramLiveState live_{&this->hub_, PAIR};
  SecTouchFan fan_{&this->hub_, PAIR_LEVEL_ID, LABEL_ID};
  std::vector<Shown> shown_;
};

/// A following fan at each normal level.
class FanFollowsNormalLevelTest : public FanFollowsTest, public ::testing::WithParamInterface<int> {};

}  // namespace

// After boot the fan's level answer comes before the clock and the block: a fan in Automatik Zeit waits for its
// running level instead of showing on for those seconds.
TEST_F(FanFollowsTest, ABootInAutomatikZeitShowsOnlyTheRunningLevel) {
  this->follow();

  this->panel_answers_every_input(AUTOMATIC_TIME_LEVEL, OFF_LEVEL, OFF_LEVEL);

  ASSERT_EQ(this->shown_.size(), 1u);
  EXPECT_FALSE(this->shown_[0].on);
  EXPECT_EQ(this->shown_[0].preset, AUTOMATIC_TIME_NAME);
}

TEST_F(FanFollowsTest, ABootOutsideAutomatikZeitShowsTheLevelAtOnce) {
  this->follow();

  this->panel_answers_pair_level(SOME_NORMAL_LEVEL);

  ASSERT_EQ(this->shown_.size(), 1u);
  this->expect_shown(true, SOME_NORMAL_LEVEL, NORMAL_PRESET_NAME);
}

TEST_F(FanFollowsTest, ABootOfAFanThatDoesNotFollowShowsAutomatikZeitAtOnce) {
  this->panel_answers_pair_level(AUTOMATIC_TIME_LEVEL);

  ASSERT_EQ(this->shown_.size(), 1u);
  EXPECT_TRUE(this->shown_[0].on);
}

TEST_F(FanFollowsTest, TheModeSensorNamesAutomaticTimeWhileTheProgramStopsTheFan) {
  text_sensor::TextSensor mode;
  this->hub_.register_text_sensor(PAIR_LEVEL_ID, &mode);
  this->follow();

  this->panel_answers_every_input(AUTOMATIC_TIME_LEVEL, OFF_LEVEL, OFF_LEVEL);
  ASSERT_FALSE(this->fan_.state);
  EXPECT_EQ(mode.get_state(), AUTOMATIC_TIME_NAME);

  this->panel_answers_pair_level(OFF_LEVEL);
  EXPECT_EQ(mode.get_state(), MODE_TEXT_OFF);
}

TEST_F(FanFollowsTest, RunningLevelOffShowsTheFanOffAtTheNextChange) {
  this->follow();
  this->panel_answers_every_input(AUTOMATIC_TIME_LEVEL, SOME_NORMAL_LEVEL, OFF_LEVEL);
  this->expect_shown(true, SOME_NORMAL_LEVEL, AUTOMATIC_TIME_NAME);
  const size_t publishes = this->shown_.size();

  this->reach_the_next_change();

  this->expect_shown(false, SOME_NORMAL_LEVEL, AUTOMATIC_TIME_NAME);
  EXPECT_EQ(this->shown_.size(), publishes + 1);
}

TEST_P(FanFollowsNormalLevelTest, ANormalRunningLevelShowsTheFanOnAtThatSpeedAtTheNextChange) {
  const int level = GetParam();
  this->follow();
  this->panel_answers_every_input(AUTOMATIC_TIME_LEVEL, OFF_LEVEL, level);
  ASSERT_FALSE(this->fan_.state);

  this->reach_the_next_change();

  this->expect_shown(true, level, AUTOMATIC_TIME_NAME);
}

INSTANTIATE_TEST_SUITE_P(NormalLevels, FanFollowsNormalLevelTest,
                         ::testing::Range(MIN_NORMAL_LEVEL, MAX_NORMAL_LEVEL + 1));

TEST_F(FanFollowsTest, TheNextChangeToTheSameDisplayPublishesNothing) {
  this->follow();
  this->panel_answers_every_input(AUTOMATIC_TIME_LEVEL, SOME_NORMAL_LEVEL, SOME_NORMAL_LEVEL);
  const size_t publishes = this->shown_.size();

  this->reach_the_next_change();

  EXPECT_EQ(this->shown_.size(), publishes);
}

// follow_time_program: false.
TEST_F(FanFollowsTest, AFanThatDoesNotFollowShowsAutomatikZeitAsOn) {
  this->panel_answers_every_input(AUTOMATIC_TIME_LEVEL, SOME_NORMAL_LEVEL, OFF_LEVEL);
  this->expect_shown(true, AUTOMATIC_TIME_LEVEL, AUTOMATIC_TIME_NAME);
  const size_t publishes = this->shown_.size();

  this->reach_the_next_change();

  this->expect_shown(true, AUTOMATIC_TIME_LEVEL, AUTOMATIC_TIME_NAME);
  EXPECT_EQ(this->shown_.size(), publishes);
}

// The live state hears a level answer before the fan does: neither order of the two shows Automatik Zeit's display
// without its running level.
TEST_F(FanFollowsTest, LeavingAutomatikZeitShowsOnlyTheNewLevel) {
  this->follow();
  this->panel_answers_every_input(AUTOMATIC_TIME_LEVEL, OFF_LEVEL, OFF_LEVEL);
  ASSERT_FALSE(this->fan_.state);
  this->shown_.clear();

  this->panel_answers_pair_level(SOME_NORMAL_LEVEL);

  ASSERT_EQ(this->shown_.size(), 1u);
  EXPECT_TRUE(this->shown_[0].on);
  EXPECT_EQ(this->shown_[0].speed, SOME_NORMAL_LEVEL);
  EXPECT_EQ(this->shown_[0].preset, NORMAL_PRESET_NAME);
}

TEST_F(FanFollowsTest, EnteringAutomatikZeitShowsOnlyTheRunningLevel) {
  this->follow();
  this->panel_answers_every_input(SOME_NORMAL_LEVEL, OFF_LEVEL, OFF_LEVEL);
  ASSERT_TRUE(this->fan_.state);
  this->shown_.clear();

  this->panel_answers_pair_level(AUTOMATIC_TIME_LEVEL);

  ASSERT_EQ(this->shown_.size(), 1u);
  EXPECT_FALSE(this->shown_[0].on);
  EXPECT_EQ(this->shown_[0].preset, AUTOMATIC_TIME_NAME);
}

// A command from HA writes the level ID as without following, which ends Automatik Zeit.
TEST_F(FanFollowsTest, AnOnWithASpeedWhileFollowingWritesThatLevel) {
  this->follow();
  this->panel_answers_every_input(AUTOMATIC_TIME_LEVEL, OFF_LEVEL, OFF_LEVEL);
  ASSERT_FALSE(this->fan_.state);

  this->fan_.make_call().set_state(true).set_speed(SOME_NORMAL_LEVEL).perform();
  this->uart_.tx.clear();
  this->send_next_task();

  EXPECT_EQ(this->uart_.tx, set_frame(PAIR_LEVEL_ID, std::to_string(SOME_NORMAL_LEVEL)));
}

// With no switch time enabled the fan shows Automatik Zeit's level as the speed, and the running level off keeps it.
TEST_F(FanFollowsTest, ABareOnFromAutomatikZeitsSpeedWritesLevelOne) {
  this->follow();
  this->panel_answers_every_input(AUTOMATIC_TIME_LEVEL, OFF_LEVEL, OFF_LEVEL);
  this->panel_answers_enabled_mask(0);
  this->drain();
  this->panel_answers_enabled_mask(ENABLED_SWITCH_TIMES);
  this->drain();
  this->expect_shown(false, AUTOMATIC_TIME_LEVEL, AUTOMATIC_TIME_NAME);

  this->fan_.turn_on().perform();
  this->uart_.tx.clear();
  this->send_next_task();

  EXPECT_EQ(this->uart_.tx, set_frame(PAIR_LEVEL_ID, std::to_string(MIN_NORMAL_LEVEL)));
}

// The pair's level ID still reads Automatik Zeit until the read-back after the SET: a clock answer before it shows
// the speed HA set, not the old running level.
TEST_F(FanFollowsTest, AClockAnswerWhileTheSetIsQueuedKeepsTheSpeedHaSet) {
  this->follow();
  this->panel_answers_every_input(AUTOMATIC_TIME_LEVEL, OFF_LEVEL, OFF_LEVEL);
  ASSERT_FALSE(this->fan_.state);
  this->hub_.add_global_setting_tasks_to_queue();
  this->send_next_task();  // the clock GET is in flight
  this->shown_.clear();

  this->fan_.make_call().set_state(true).set_speed(SOME_NORMAL_LEVEL).perform();
  this->panel_answers_get(CLOCK_PROPERTY_ID, CLOCK);

  this->expect_every_publish_on_at(SOME_NORMAL_LEVEL);
}

// HA sets Automatik Zeit's level as the speed, so the fan's level is Automatik Zeit's before the read-back: showing on
// this answer would show the old running level instead of what HA set.
TEST_F(FanFollowsTest, AClockAnswerWhileTheSetIsQueuedPublishesNothing) {
  this->follow();
  this->panel_answers_every_input(AUTOMATIC_TIME_LEVEL, OFF_LEVEL, OFF_LEVEL);
  ASSERT_FALSE(this->fan_.state);
  this->hub_.add_global_setting_tasks_to_queue();
  this->send_next_task();  // the clock GET is in flight
  this->fan_.make_call().set_state(true).set_speed(AUTOMATIC_TIME_LEVEL).perform();
  const size_t publishes = this->shown_.size();

  this->panel_answers_get(CLOCK_PROPERTY_ID, CLOCK);

  EXPECT_EQ(this->shown_.size(), publishes);
}

TEST_F(FanFollowsTest, AClockAnswerAfterTheSetWasAnsweredKeepsTheSpeedHaSet) {
  this->follow();
  this->panel_answers_every_input(AUTOMATIC_TIME_LEVEL, OFF_LEVEL, OFF_LEVEL);
  ASSERT_FALSE(this->fan_.state);
  this->shown_.clear();
  this->fan_.make_call().set_state(true).set_speed(SOME_NORMAL_LEVEL).perform();
  this->send_next_task();  // the SET
  this->uart_.push_rx(ack_frame());
  this->hub_.loop_times(3);

  this->hub_.add_global_setting_tasks_to_queue();
  this->send_next_task();
  this->panel_answers_get(CLOCK_PROPERTY_ID, CLOCK);

  this->expect_every_publish_on_at(SOME_NORMAL_LEVEL);
}

// A needed input never read well leaves the live state with no state: the fan drops the running level it showed.
TEST_F(FanFollowsTest, AFanShownOffShowsAsNotFollowingWhenTheLiveStateHasNoState) {
  this->follow();
  this->panel_answers_every_input_but(NEVER_READ_SWITCH_TIME, AUTOMATIC_TIME_LEVEL, OFF_LEVEL, OFF_LEVEL);
  ASSERT_FALSE(this->fan_.state);

  this->panel_answers_enabled_mask(ENABLED_SWITCH_TIMES | switch_time_bit(NEVER_READ_SWITCH_TIME));

  this->expect_shown(true, AUTOMATIC_TIME_LEVEL, AUTOMATIC_TIME_NAME);
}

// One live state per pair, shared by the fan and the live-state entities: each ID is listened to and polled once.
TEST_F(FanFollowsTest, AFollowingFanAddsNoListenerOrPoll) {
  text_sensor::TextSensor running_level;
  this->live_.set_running_level(&running_level);
  this->follow();

  for (int id : block_ids()) {
    SCOPED_TRACE(id);
    EXPECT_EQ(this->hub_.string_update_listeners.count(id), 1u);
    EXPECT_EQ(this->hub_.recursive_update_listeners.count(id), 0u);
  }
  EXPECT_EQ(this->hub_.string_update_listeners.count(PAIR_LEVEL_ID), 1u);
  EXPECT_EQ(this->hub_.recursive_update_listeners.count(PAIR_LEVEL_ID), 1u);
  EXPECT_EQ(this->hub_.string_update_listeners.count(CLOCK_PROPERTY_ID), 1u);
  EXPECT_EQ(this->hub_.time_program_polls_[PAIR - 1].size(), TIME_PROGRAM_IDS_PER_PAIR);
}

}  // namespace esphome::sec_touch::testing
