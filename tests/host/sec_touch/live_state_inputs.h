#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common.h"
#include "esphome/components/sec_touch/fan_level.h"
#include "esphome/components/sec_touch/time_program_ids.h"
#include "esphome/components/sec_touch/time_units.h"
#include "esphome/core/hal.h"

namespace esphome::sec_touch::testing {

constexpr int PAIR = 1;
inline const int PAIR_LEVEL_ID = FAN_LEVEL_IDS[PAIR - 1];

/// Switch times 1 and 2 enabled. The others are off.
constexpr uint8_t ENABLED_SWITCH_TIMES = switch_time_bit(1) | switch_time_bit(2);
inline const std::string MASK = mask_text(ENABLED_SWITCH_TIMES);
inline const char *const TIMES[SWITCH_TIME_COUNT] = {"08:00:00", "12:00:00", "18:00:00", "21:00:00", "23:00:00"};
/// A second before switch time 2: switch time 1 is in effect, switch time 2 is the next change.
inline const std::string CLOCK = "11:59:59";
/// A time answer the live state can't read.
inline const std::string NOT_A_TIME = "not a time";
/// Past the next change, a second after the clock read.
constexpr uint32_t PAST_THE_NEXT_CHANGE_MS = MS_PER_S + 100;
/// The levels switch times 1 and 2 run unless a test picks others.
constexpr int FIRST_LEVEL = 1;
constexpr int SECOND_LEVEL = 2;

/// The pair's whole block, in the order the live state puts it on the poll: the mask, the times, the levels.
inline std::vector<int> block_ids() {
  std::vector<int> ids{time_program_enabled_id(PAIR)};
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    ids.push_back(time_program_time_id(PAIR, n));
  }
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    ids.push_back(time_program_level_id(PAIR, n));
  }
  return ids;
}

/// The IDs one boot poll queues, in order: the fan's level ID, the panel clock, then the pair's block.
inline std::vector<int> boot_poll_ids() {
  std::vector<int> ids{PAIR_LEVEL_ID, CLOCK_PROPERTY_ID};
  for (int id : block_ids()) {
    ids.push_back(id);
  }
  return ids;
}

/// What the panel holds for `id`: the pair at `pair_level`, switch time 1 running `first`, the others `second`.
inline std::string panel_value(int id, int pair_level = AUTOMATIC_TIME_LEVEL, int first = FIRST_LEVEL,
                               int second = SECOND_LEVEL) {
  if (id == PAIR_LEVEL_ID) {
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

/// Pair PAIR's live-state inputs, read through the real hub with a fake bus.
class LiveStateInputsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    this->hub_.set_uart_parent(&this->uart_);
    this->hub_.set_inter_task_delay(0);
    this->hub_.last_time_programs_update_ms_ = millis();
    this->hub_.last_global_settings_update_ms_ = millis();
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
    this->hub_.loop_times(3);
  }

  /// Queues every input once, as the boot poll does, and the panel answers each with `panel_value`.
  void panel_answers_every_input(int pair_level = AUTOMATIC_TIME_LEVEL, int first = FIRST_LEVEL,
                                 int second = SECOND_LEVEL) {
    this->panel_answers_every_input_but(std::nullopt, pair_level, first, second);
  }

  /// As panel_answers_every_input, but switch time `never_read`'s time answers a value that isn't a time.
  void panel_answers_every_input_but(std::optional<int> never_read, int pair_level = AUTOMATIC_TIME_LEVEL,
                                     int first = FIRST_LEVEL, int second = SECOND_LEVEL) {
    this->hub_.add_recursive_tasks_to_get_queue();
    this->hub_.add_global_setting_tasks_to_queue();
    this->hub_.add_time_program_tasks_to_queue(PAIR);
    for (int id : boot_poll_ids()) {
      this->send_next_task();
      const bool unreadable = never_read.has_value() && id == time_program_time_id(PAIR, *never_read);
      this->panel_answers_get(id, unreadable ? NOT_A_TIME : panel_value(id, pair_level, first, second));
    }
  }

  /// The pair's time-program poll, its mask GET answered with `enabled`. The rest of the block stays queued.
  void panel_answers_enabled_mask(uint8_t enabled) {
    this->hub_.add_time_program_tasks_to_queue(PAIR);
    this->send_next_task();
    this->panel_answers_get(time_program_enabled_id(PAIR), mask_text(enabled));
  }

  FakeUART uart_;
  TestableSECTouch hub_;
};

}  // namespace esphome::sec_touch::testing
