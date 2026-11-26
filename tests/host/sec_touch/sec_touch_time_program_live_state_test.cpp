#include <gtest/gtest.h>

#include <cmath>
#include <ctime>
#include <string>
#include <vector>

#include "common.h"
#include "esphome/components/sec_touch/time_program_codec.h"
#include "esphome/components/sec_touch/time_program_ids.h"
#include "esphome/components/sec_touch/time_program_live_model.h"
#include "esphome/components/sec_touch/time_program_live_state.h"
#include "esphome/core/hal.h"

#if !defined(USE_SENSOR) || !defined(USE_TIME)
#error "next_change needs sensor and time in the host build (tests/host/sec_touch/sensor/ and benchmark.yaml)"
#endif

namespace esphome::sec_touch::testing {

namespace {

constexpr int PAIR = 1;
const int PAIR_LEVEL_ID = FAN_LEVEL_IDS[PAIR - 1];

constexpr uint32_t GLOBAL_INTERVAL_MS = DEFAULT_GLOBAL_SETTINGS_UPDATE_INTERVAL_MS;
constexpr uint32_t INTERVAL_MS = TIME_PROGRAMS_INTERVAL_MS;

using known_property_detail::level_name;

/// The pair's level ID in Automatik Zeit.
const std::string RUNNING = std::to_string(AUTOMATIC_TIME_LEVEL);
/// Switch times 1 and 2 enabled, at 08:00 and 12:00, levels 1 and 2. The others are off.
const std::string MASK = std::string(format_enabled_mask(switch_time_bit(1) | switch_time_bit(2)).data());
const char *const TIMES[SWITCH_TIME_COUNT] = {"08:00:00", "12:00:00", "18:00:00", "21:00:00", "23:00:00"};
/// A second before switch time 2.
const std::string CLOCK = "11:59:59";
const std::string DATE = "26.11.26";

/// A whole minute since the epoch, the time source's now.
constexpr time_t NOW_S = 1800000000;

/// The pair's whole block, in the order the live state puts it on the poll: the mask, the times, the levels.
std::vector<int> block_ids() {
  std::vector<int> ids{time_program_enabled_id(PAIR)};
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    ids.push_back(time_program_time_id(PAIR, n));
  }
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    ids.push_back(time_program_level_id(PAIR, n));
  }
  return ids;
}

/// What the panel holds for each ID the boot poll queues.
std::string panel_value(int id, const std::string &pair_level) {
  if (id == PAIR_LEVEL_ID) {
    return pair_level;
  }
  if (id == DATE_PROPERTY_ID) {
    return DATE;
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
      return std::to_string(slot->switch_time);
  }
  return "";
}

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
class LiveStateTest : public ::testing::Test {
 protected:
  void SetUp() override {
    this->hub_.set_uart_parent(&this->uart_);
    this->hub_.set_inter_task_delay(0);
    this->hub_.set_global_settings_update_interval_ms(GLOBAL_INTERVAL_MS);
    this->hub_.set_time_programs_update_interval_ms(INTERVAL_MS);
    this->hub_.last_time_programs_update_ms_ = millis();
    this->hub_.last_global_settings_update_ms_ = millis();
    // Stands in for the pair's fan entry, which a live-state entity requires.
    this->hub_.register_recursive_update_listener(PAIR_LEVEL_ID, [](int, int) {});

    this->live_.set_switch_time_in_effect(&this->in_effect_);
    this->live_.set_running_level(&this->running_level_);
    this->live_.set_next_level(&this->next_level_);
    this->live_.set_next_change(&this->next_change_, &this->clock_);
    this->in_effect_.add_on_state_callback([this](const std::string &) { this->in_effect_publishes_++; });
    this->running_level_.add_on_state_callback([this](const std::string &) { this->running_level_publishes_++; });
    this->next_level_.add_on_state_callback([this](const std::string &) { this->next_level_publishes_++; });
    this->next_change_.add_on_state_callback([this](float) { this->next_change_publishes_++; });
  }

  void loop_times(int n) {
    for (int i = 0; i < n; i++) {
      this->hub_.loop();
    }
  }

  /// Sends the first queued task. Returns what went out.
  std::vector<uint8_t> send_next_task() {
    this->uart_.tx.clear();
    this->hub_.loop();
    return this->uart_.tx;
  }

  void panel_answers_get(int property_id, const std::string &value) {
    this->uart_.push_rx(ack_frame());
    this->uart_.push_rx(data_frame(property_id, value));
    this->loop_times(3);
  }

  /// Sends every queued task, each left unanswered. Returns each request, in order.
  std::vector<std::vector<uint8_t>> drain() {
    std::vector<std::vector<uint8_t>> sent;
    std::vector<uint8_t> next = this->send_next_task();
    while (!next.empty()) {
      sent.push_back(next);
      this->uart_.tx.clear();
      this->hub_.task_start_time_ = millis() - (TASK_TIMEOUT_MS + 1);
      this->loop_times(2);
      next = this->uart_.tx;
    }
    return sent;
  }

  /// The IDs one boot poll queues, in order: the fan's level ID, the global settings, then the pair's block.
  static std::vector<int> boot_poll_ids() {
    std::vector<int> ids{PAIR_LEVEL_ID, DATE_PROPERTY_ID, CLOCK_PROPERTY_ID};
    for (int id : block_ids()) {
      ids.push_back(id);
    }
    return ids;
  }

  /// Queues every input once, as the boot poll does, and the panel answers each.
  void panel_answers_every_input(const std::string &pair_level = RUNNING) {
    this->hub_.add_recursive_tasks_to_get_queue();
    this->hub_.add_global_setting_tasks_to_queue();
    this->hub_.add_time_program_tasks_to_queue(PAIR);
    for (int id : boot_poll_ids()) {
      this->send_next_task();
      this->panel_answers_get(id, panel_value(id, pair_level));
    }
  }

  FakeUART uart_;
  TestableSECTouch hub_;
  FakeClock clock_;
  TestableLiveState live_{&this->hub_, PAIR};
  text_sensor::TextSensor in_effect_;
  text_sensor::TextSensor running_level_;
  text_sensor::TextSensor next_level_;
  sensor::Sensor next_change_;
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

// Spec "Reading": with a live-state entity, the pair's whole block and IDs 1 and 2 are polled.
TEST_F(LiveStateTest, BootPollsTheWholeBlockAndThePanelClock) {
  this->hub_.setup();

  EXPECT_EQ(this->drain(), frames_of(boot_poll_ids()));
}

// Spec "Reading": IDs 1 and 2 are polled once, also with clock_drift listening to them, date before clock.
TEST_F(LiveStateTest, ClockDriftSharesThePanelClockPoll) {
  this->hub_.register_global_setting_string_listener(DATE_PROPERTY_ID, [](int, const std::string &) {});
  this->hub_.register_global_setting_string_listener(CLOCK_PROPERTY_ID, [](int, const std::string &) {});
  this->hub_.setup();
  this->drain();

  this->hub_.last_global_settings_update_ms_ = millis() - (GLOBAL_INTERVAL_MS + 1);
  this->hub_.loop();  // an idle loop queues the due poll

  EXPECT_EQ(this->drain(), frames_of({DATE_PROPERTY_ID, CLOCK_PROPERTY_ID}));
}

TEST_F(LiveStateTest, NothingIsPublishedBeforeEveryInputWasRead) {
  this->hub_.add_time_program_tasks_to_queue(PAIR);
  for (int id : block_ids()) {
    this->send_next_task();
    this->panel_answers_get(id, panel_value(id, RUNNING));
  }

  EXPECT_FALSE(this->in_effect_.has_state());
  EXPECT_FALSE(this->next_change_.has_state());
}

TEST_F(LiveStateTest, PublishesOnceEveryInputWasRead) {
  this->panel_answers_every_input();

  EXPECT_EQ(this->in_effect_.get_state(), switch_time_name(1));
  EXPECT_EQ(this->running_level_.get_state(), level_name(1));
  EXPECT_EQ(this->next_level_.get_state(), level_name(2));
  // The next change is a second away: now, rounded to the minute.
  EXPECT_FLOAT_EQ(this->next_change_.get_state(), static_cast<float>(NOW_S));
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

TEST_F(LiveStateTest, NotRunningOutsideAutomatikZeit) {
  this->panel_answers_every_input(std::to_string(MAX_NORMAL_LEVEL));

  EXPECT_EQ(this->in_effect_.get_state(), LIVE_STATE_NOT_RUNNING);
  EXPECT_EQ(this->running_level_.get_state(), LIVE_STATE_NOT_RUNNING);
  EXPECT_EQ(this->next_level_.get_state(), LIVE_STATE_NOT_RUNNING);
  EXPECT_TRUE(std::isnan(this->next_change_.get_state()));
}

// Spec "Live state": one timeout set for the next change recomputes.
TEST_F(LiveStateTest, TheNextChangeRecomputes) {
  this->panel_answers_every_input();
  ASSERT_EQ(this->in_effect_.get_state(), switch_time_name(1));

  delay(MS_PER_S + 100);  // past the next change, a second after the clock read
  this->live_.loop();

  EXPECT_EQ(this->in_effect_.get_state(), switch_time_name(2));
  EXPECT_EQ(this->running_level_.get_state(), level_name(2));
  EXPECT_EQ(this->next_level_.get_state(), level_name(1));
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

  EXPECT_FLOAT_EQ(this->next_change_.get_state(), static_cast<float>(NOW_S));
}

}  // namespace esphome::sec_touch::testing
