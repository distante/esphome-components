#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "common.h"
#include "esphome/components/sec_touch/fan/sec_touch_fan.h"
#include "esphome/components/sec_touch/fan_level.h"
#include "esphome/components/sec_touch/time_program_ids.h"
#include "esphome/components/sec_touch/time_program_live_state.h"
#include "esphome/core/hal.h"

// .specs/time-programs.md, "Live state": in Automatik Zeit a following fan shows its pair's running level.

namespace esphome::sec_touch::testing {

namespace {

constexpr int PAIR = 1;
const int LEVEL_ID = FAN_LEVEL_IDS[PAIR - 1];
const int LABEL_ID = FAN_LABEL_IDS[PAIR - 1];

/// Switch times 1 and 2 enabled, at 08:00 and 12:00. The others are off.
const std::string MASK = mask_text(switch_time_bit(1) | switch_time_bit(2));
const char *const TIMES[SWITCH_TIME_COUNT] = {"08:00:00", "12:00:00", "18:00:00", "21:00:00", "23:00:00"};
/// A second before switch time 2: switch time 1 is in effect, switch time 2 is the next change.
const std::string CLOCK = "11:59:59";
/// Past the next change, a second after the clock read.
constexpr uint32_t PAST_THE_NEXT_CHANGE_MS = MS_PER_S + 100;

/// What the fan showed at one publish.
struct Shown {
  bool on;
  int speed;
  std::string preset;
};

/// Pair PAIR's fan and live state, driven through the real hub with a fake bus.
class FanFollowsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    this->hub_.set_uart_parent(&this->uart_);
    this->hub_.set_inter_task_delay(0);
    this->hub_.last_time_programs_update_ms_ = millis();
    this->hub_.last_global_settings_update_ms_ = millis();
    this->fan_.setup();
    this->fan_.add_on_state_callback([this]() {
      this->shown_.push_back(
          Shown{this->fan_.state, this->fan_.speed, std::string(this->fan_.get_preset_mode().c_str())});
    });
  }

  void follow() { this->fan_.follow_time_program(&this->live_); }

  void send_next_task() { this->hub_.loop(); }

  void panel_answers_get(int property_id, const std::string &value) {
    this->uart_.push_rx(ack_frame());
    this->uart_.push_rx(data_frame(property_id, value));
    this->hub_.loop_times(3);
  }

  /// What the panel holds for `id`: switch time 1 runs `first`, switch time 2 runs `second`.
  std::string panel_value(int id, int pair_level, int first, int second) const {
    if (id == LEVEL_ID) {
      return std::to_string(pair_level);
    }
    if (id == CLOCK_PROPERTY_ID) {
      return CLOCK;
    }
    auto slot = time_program_slot(id);
    switch (slot->field) {
      case TimeProgramField::ENABLED:
        return MASK;
      case TimeProgramField::TIME:
        return TIMES[slot->switch_time - 1];
      case TimeProgramField::LEVEL:
        return std::to_string(slot->switch_time == 1 ? first : second);
    }
    return "";
  }

  /// One boot poll, each GET answered: the fan's level ID, the panel clock, then the pair's block.
  void panel_answers_every_input(int pair_level, int first, int second) {
    this->hub_.add_recursive_tasks_to_get_queue();
    this->hub_.add_global_setting_tasks_to_queue();
    this->hub_.add_time_program_tasks_to_queue(PAIR);
    std::vector<int> ids{LEVEL_ID, CLOCK_PROPERTY_ID, time_program_enabled_id(PAIR)};
    for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
      ids.push_back(time_program_time_id(PAIR, n));
    }
    for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
      ids.push_back(time_program_level_id(PAIR, n));
    }
    for (int id : ids) {
      this->send_next_task();
      this->panel_answers_get(id, this->panel_value(id, pair_level, first, second));
    }
  }

  /// The fan's level poll, answered with `level`.
  void panel_answers_pair_level(int level) {
    this->hub_.add_recursive_tasks_to_get_queue();
    this->send_next_task();
    this->panel_answers_get(LEVEL_ID, std::to_string(level));
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

  FakeUART uart_;
  TestableSECTouch hub_;
  TimeProgramLiveState live_{&this->hub_, PAIR};
  SecTouchFan fan_{&this->hub_, LEVEL_ID, LABEL_ID};
  std::vector<Shown> shown_;
};

/// A following fan at each normal level.
class FanFollowsNormalLevelTest : public FanFollowsTest, public ::testing::WithParamInterface<int> {};

}  // namespace

TEST_F(FanFollowsTest, RunningLevelOffShowsTheFanOffAtTheNextChange) {
  this->follow();
  this->panel_answers_every_input(AUTOMATIC_TIME_LEVEL, 3, OFF_LEVEL);
  this->expect_shown(true, 3, AUTOMATIC_TIME_NAME);
  const size_t publishes = this->shown_.size();

  this->reach_the_next_change();

  this->expect_shown(false, 3, AUTOMATIC_TIME_NAME);
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
  this->panel_answers_every_input(AUTOMATIC_TIME_LEVEL, 3, 3);
  const size_t publishes = this->shown_.size();

  this->reach_the_next_change();

  EXPECT_EQ(this->shown_.size(), publishes);
}

// follow_time_program: false.
TEST_F(FanFollowsTest, AFanThatDoesNotFollowShowsAutomatikZeitAsOn) {
  this->panel_answers_every_input(AUTOMATIC_TIME_LEVEL, 3, OFF_LEVEL);
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

  this->panel_answers_pair_level(3);

  ASSERT_EQ(this->shown_.size(), 1u);
  EXPECT_TRUE(this->shown_[0].on);
  EXPECT_EQ(this->shown_[0].speed, 3);
  EXPECT_EQ(this->shown_[0].preset, NORMAL_PRESET_NAME);
}

TEST_F(FanFollowsTest, EnteringAutomatikZeitShowsOnlyTheRunningLevel) {
  this->follow();
  this->panel_answers_every_input(3, OFF_LEVEL, OFF_LEVEL);
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

  this->fan_.make_call().set_state(true).set_speed(2).perform();
  this->uart_.tx.clear();
  this->send_next_task();

  EXPECT_EQ(this->uart_.tx, set_frame(LEVEL_ID, "2"));
}

// The boot shows Automatik Zeit's level as the speed before the running level is known, and off keeps it.
TEST_F(FanFollowsTest, ABareOnFromAutomatikZeitsSpeedWritesLevelOne) {
  this->follow();
  this->panel_answers_every_input(AUTOMATIC_TIME_LEVEL, OFF_LEVEL, OFF_LEVEL);
  this->expect_shown(false, AUTOMATIC_TIME_LEVEL, AUTOMATIC_TIME_NAME);

  this->fan_.turn_on().perform();
  this->uart_.tx.clear();
  this->send_next_task();

  EXPECT_EQ(this->uart_.tx, set_frame(LEVEL_ID, std::to_string(MIN_NORMAL_LEVEL)));
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

  this->fan_.make_call().set_state(true).set_speed(3).perform();
  this->panel_answers_get(CLOCK_PROPERTY_ID, CLOCK);

  this->expect_every_publish_on_at(3);
}

// HA sets Automatik Zeit's level as the speed: the running level shows once the level ID reads it back.
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
  this->fan_.make_call().set_state(true).set_speed(3).perform();
  this->send_next_task();  // the SET
  this->uart_.push_rx(ack_frame());
  this->hub_.loop_times(3);

  this->hub_.add_global_setting_tasks_to_queue();
  this->send_next_task();
  this->panel_answers_get(CLOCK_PROPERTY_ID, CLOCK);

  this->expect_every_publish_on_at(3);
}

// One live state per pair, shared by the fan and the live-state entities: each ID is listened to and polled once.
TEST_F(FanFollowsTest, AFollowingFanAddsNoListenerOrPoll) {
  text_sensor::TextSensor running_level;
  this->live_.set_running_level(&running_level);
  this->follow();

  std::vector<int> block{time_program_enabled_id(PAIR)};
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    block.push_back(time_program_time_id(PAIR, n));
    block.push_back(time_program_level_id(PAIR, n));
  }
  for (int id : block) {
    SCOPED_TRACE(id);
    EXPECT_EQ(this->hub_.string_update_listeners.count(id), 1u);
    EXPECT_EQ(this->hub_.recursive_update_listeners.count(id), 0u);
  }
  EXPECT_EQ(this->hub_.string_update_listeners.count(LEVEL_ID), 1u);
  EXPECT_EQ(this->hub_.recursive_update_listeners.count(LEVEL_ID), 1u);
  EXPECT_EQ(this->hub_.string_update_listeners.count(CLOCK_PROPERTY_ID), 1u);
  EXPECT_EQ(this->hub_.time_program_polls_[PAIR - 1].size(), TIME_PROGRAM_IDS_PER_PAIR);
}

}  // namespace esphome::sec_touch::testing
