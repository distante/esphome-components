#include <gtest/gtest.h>

#include <ctime>
#include <string>
#include <vector>

#include "common.h"
#include "live_state_inputs.h"
#include "esphome/components/sec_touch/time_program_codec.h"
#include "esphome/components/sec_touch/time_program_ids.h"
#include "esphome/components/sec_touch/time_program_live_model.h"
#include "esphome/components/sec_touch/time_program_live_state.h"
#include "esphome/core/hal.h"

#ifndef USE_TIME
#error "next_change needs time in the host build (benchmark.yaml)"
#endif

namespace esphome::sec_touch::testing {

namespace {

using known_property_detail::level_name;

/// The pair's level ID in Automatik Zeit.
const std::string RUNNING = std::to_string(AUTOMATIC_TIME_LEVEL);
/// A whole minute since the epoch, the time source's now.
constexpr time_t NOW_S = 1800000000;

class FakeClock : public time::RealTimeClock {
 public:
  time_t timestamp_now() override { return this->now_s; }
  void update() override {}
  time_t now_s{NOW_S};
};

class TestableLiveState : public TimeProgramLiveState {
 public:
  using TimeProgramLiveState::TimeProgramLiveState;
};

/// Pair PAIR with every live-state entity, its level ID polled as its fan entry would, driven through the real hub
/// with a fake bus.
class LiveStateTest : public LiveStateInputsTest {
 protected:
  void SetUp() override {
    LiveStateInputsTest::SetUp();
    // Stands in for the pair's fan entry, which a live-state entity requires.
    this->hub_.register_recursive_update_listener(PAIR_LEVEL_ID, [](int, int) {});

    this->live_.set_switch_time_in_effect(&this->in_effect_);
    this->live_.set_running_level(&this->running_level_);
    this->live_.set_next_level(&this->next_level_);
    this->live_.set_next_change(&this->next_change_, &this->clock_);
    this->in_effect_.add_on_state_callback([this](const std::string &) { this->in_effect_publishes_++; });
    this->running_level_.add_on_state_callback([this](const std::string &) { this->running_level_publishes_++; });
    this->next_level_.add_on_state_callback([this](const std::string &) { this->next_level_publishes_++; });
    this->next_change_.add_on_state_callback([this](const std::string &) { this->next_change_publishes_++; });
  }

  /// Sends every queued task, each left unanswered. Returns each request, in order.
  std::vector<std::vector<uint8_t>> drain() {
    std::vector<std::vector<uint8_t>> sent;
    std::vector<uint8_t> next = this->send_next_task();
    while (!next.empty()) {
      sent.push_back(next);
      this->uart_.tx.clear();
      this->hub_.age_task_past_watchdog();
      this->hub_.loop_times(2);
      next = this->uart_.tx;
    }
    return sent;
  }

  FakeClock clock_;
  TestableLiveState live_{&this->hub_, PAIR};
  text_sensor::TextSensor in_effect_;
  text_sensor::TextSensor running_level_;
  text_sensor::TextSensor next_level_;
  text_sensor::TextSensor next_change_;
  int in_effect_publishes_{0};
  int running_level_publishes_{0};
  int next_level_publishes_{0};
  int next_change_publishes_{0};
};

std::vector<std::vector<uint8_t>> frames_of(const std::vector<int> &ids) {
  std::vector<std::vector<uint8_t>> frames;
  for (int id : ids) {
    frames.push_back(get_frame(id));
  }
  return frames;
}

}  // namespace

// Spec "Reading": with a live-state entity, the pair's whole block and the panel clock (ID 2) are polled; the panel
// date (ID 1) is not.
TEST_F(LiveStateTest, BootPollsTheWholeBlockAndThePanelClock) {
  this->hub_.setup();

  EXPECT_EQ(this->drain(), frames_of(boot_poll_ids()));
}

// Spec "Reading": with clock_drift, which registers after the live state, IDs 1 and 2 are polled once each, date
// right before clock.
TEST_F(LiveStateTest, ClockDriftSharesThePanelClockPoll) {
  this->hub_.register_global_setting_string_listener(DATE_PROPERTY_ID, [](int, const std::string &) {});
  this->hub_.register_global_setting_string_listener(CLOCK_PROPERTY_ID, [](int, const std::string &) {});
  this->hub_.setup();
  this->drain();

  this->hub_.last_global_settings_update_ms_ = millis() - (TEST_GLOBAL_SETTINGS_INTERVAL_MS + 1);
  this->hub_.loop();  // an idle loop queues the due poll

  EXPECT_EQ(this->drain(), frames_of({DATE_PROPERTY_ID, CLOCK_PROPERTY_ID}));
}

// Spec "Live state", Inputs: the pair level is always needed.
TEST_F(LiveStateTest, NothingIsPublishedBeforeThePairLevelWasRead) {
  this->hub_.add_time_program_tasks_to_queue(PAIR);
  for (int id : block_ids()) {
    this->send_next_task();
    this->panel_answers_get(id, panel_value(id));
  }

  EXPECT_FALSE(this->in_effect_.has_state());
  EXPECT_FALSE(this->next_change_.has_state());
}

TEST_F(LiveStateTest, PublishesOnceEveryInputWasRead) {
  this->panel_answers_every_input();

  EXPECT_EQ(this->in_effect_.get_state(), switch_time_name(1));
  EXPECT_EQ(this->running_level_.get_state(), level_name(FIRST_LEVEL));
  EXPECT_EQ(this->next_level_.get_state(), level_name(SECOND_LEVEL));
  // The next change is a second away: now, rounded to the minute.
  EXPECT_EQ(this->next_change_.get_state(), next_change_iso(NOW_S).text.data());
}

// Spec "Live state": an entity publishes only when its value changes.
TEST_F(LiveStateTest, TheSameReadsAgainPublishNothing) {
  this->panel_answers_every_input();
  this->panel_answers_every_input();

  EXPECT_EQ(this->in_effect_publishes_, 1);
  EXPECT_EQ(this->running_level_publishes_, 1);
  EXPECT_EQ(this->next_level_publishes_, 1);
  EXPECT_EQ(this->next_change_publishes_, 1);
}

// Spec "Live state", Inputs: not running needs the pair level alone.
TEST_F(LiveStateTest, NotRunningNeedsOnlyThePairLevel) {
  this->hub_.add_recursive_tasks_to_get_queue();
  this->send_next_task();
  this->panel_answers_get(PAIR_LEVEL_ID, std::to_string(MAX_NORMAL_LEVEL));

  EXPECT_EQ(this->in_effect_.get_state(), LIVE_STATE_NOT_RUNNING);
  EXPECT_EQ(this->running_level_.get_state(), LIVE_STATE_NOT_RUNNING);
  EXPECT_EQ(this->next_level_.get_state(), LIVE_STATE_NOT_RUNNING);
}

// Spec "Live state", cases: a needed input that was never read well shows no state, also after a value was shown.
TEST_F(LiveStateTest, EnteringAutomatikZeitBeforeAnyClockReadClearsTheState) {
  this->hub_.add_recursive_tasks_to_get_queue();
  this->send_next_task();
  this->panel_answers_get(PAIR_LEVEL_ID, std::to_string(MAX_NORMAL_LEVEL));
  ASSERT_EQ(this->in_effect_.get_state(), LIVE_STATE_NOT_RUNNING);
  this->hub_.add_time_program_tasks_to_queue(PAIR);
  for (int id : block_ids()) {
    this->send_next_task();
    this->panel_answers_get(id, panel_value(id));
  }

  this->hub_.add_recursive_tasks_to_get_queue();
  this->send_next_task();
  this->panel_answers_get(PAIR_LEVEL_ID, RUNNING);

  EXPECT_FALSE(this->in_effect_.has_state());
  EXPECT_FALSE(this->running_level_.has_state());
  EXPECT_FALSE(this->next_level_.has_state());
  EXPECT_FALSE(this->next_change_.has_state());
}

// Spec "Live state", cases: enabling a switch time whose time never read well leaves a needed input unknown.
TEST_F(LiveStateTest, EnablingASwitchTimeWhoseTimeNeverReadWellClearsTheState) {
  constexpr int NEVER_READ = 3;
  const int never_read_time_id = time_program_time_id(PAIR, NEVER_READ);
  this->hub_.add_recursive_tasks_to_get_queue();
  this->hub_.add_global_setting_tasks_to_queue();
  this->hub_.add_time_program_tasks_to_queue(PAIR);
  for (int id : boot_poll_ids()) {
    this->send_next_task();
    this->panel_answers_get(id, id == never_read_time_id ? "not a time" : panel_value(id));
  }
  ASSERT_EQ(this->in_effect_.get_state(), switch_time_name(1));
  ASSERT_TRUE(this->next_change_.has_state());
  const uint8_t enabled = switch_time_bit(1) | switch_time_bit(2) | switch_time_bit(NEVER_READ);

  // Read twice: the second read finds the state already cleared and publishes nothing.
  for (int read = 0; read < 2; read++) {
    this->hub_.add_time_program_tasks_to_queue(PAIR);
    this->send_next_task();  // the mask GET
    this->panel_answers_get(time_program_enabled_id(PAIR), format_enabled_mask(enabled).data());
    this->drain();
  }

  EXPECT_FALSE(this->in_effect_.has_state());
  EXPECT_FALSE(this->running_level_.has_state());
  EXPECT_FALSE(this->next_level_.has_state());
  EXPECT_FALSE(this->next_change_.has_state());
  EXPECT_EQ(this->next_change_publishes_, 1) << "the value; a clear calls no state callback";
}

TEST_F(LiveStateTest, NotRunningOutsideAutomatikZeit) {
  this->panel_answers_every_input(MAX_NORMAL_LEVEL);

  EXPECT_EQ(this->in_effect_.get_state(), LIVE_STATE_NOT_RUNNING);
  EXPECT_EQ(this->running_level_.get_state(), LIVE_STATE_NOT_RUNNING);
  EXPECT_EQ(this->next_level_.get_state(), LIVE_STATE_NOT_RUNNING);
  EXPECT_FALSE(this->next_change_.has_state());
}

// Spec "Live state", cases: not running has no next change, also after one was shown.
TEST_F(LiveStateTest, LeavingAutomatikZeitClearsTheNextChange) {
  this->panel_answers_every_input();
  ASSERT_TRUE(this->next_change_.has_state());

  this->hub_.add_recursive_tasks_to_get_queue();
  this->send_next_task();
  this->panel_answers_get(PAIR_LEVEL_ID, std::to_string(MAX_NORMAL_LEVEL));

  EXPECT_EQ(this->in_effect_.get_state(), LIVE_STATE_NOT_RUNNING);
  EXPECT_FALSE(this->next_change_.has_state());
}

// Spec "Live state": one timeout set for the next change recomputes.
TEST_F(LiveStateTest, TheNextChangeRecomputes) {
  this->panel_answers_every_input();
  ASSERT_EQ(this->in_effect_.get_state(), switch_time_name(1));

  delay(PAST_THE_NEXT_CHANGE_MS);
  this->live_.loop();

  EXPECT_EQ(this->in_effect_.get_state(), switch_time_name(2));
  EXPECT_EQ(this->running_level_.get_state(), level_name(SECOND_LEVEL));
  EXPECT_EQ(this->next_level_.get_state(), level_name(FIRST_LEVEL));
  EXPECT_EQ(this->in_effect_publishes_, 2);
}

TEST_F(LiveStateTest, ALoopBeforeTheNextChangePublishesNothing) {
  this->panel_answers_every_input();

  this->live_.loop();

  EXPECT_EQ(this->in_effect_publishes_, 1);
}

// The pair's level ID while a SET of it is queued: the answer is older than the SET, as the fan treats it.
TEST_F(LiveStateTest, AnAnswerWhileASetIsQueuedIsSkipped) {
  this->panel_answers_every_input();
  this->hub_.add_recursive_tasks_to_get_queue();
  this->send_next_task();  // the level GET is in flight
  this->hub_.add_set_task_latest_wins(TaskTargetType::LEVEL, PAIR_LEVEL_ID, "3");

  this->panel_answers_get(PAIR_LEVEL_ID, std::to_string(MAX_NORMAL_LEVEL));

  EXPECT_EQ(this->in_effect_.get_state(), switch_time_name(1));
}

TEST_F(LiveStateTest, NextChangeWaitsForTheTimeSource) {
  this->clock_.now_s = 0;  // not synced
  this->panel_answers_every_input();
  EXPECT_FALSE(this->next_change_.has_state());
  EXPECT_EQ(this->in_effect_.get_state(), switch_time_name(1));

  this->clock_.now_s = NOW_S;
  this->live_.loop();

  EXPECT_EQ(this->next_change_.get_state(), next_change_iso(NOW_S).text.data());
}

}  // namespace esphome::sec_touch::testing
