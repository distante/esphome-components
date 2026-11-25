#pragma once

// A fan pair's time program in HA (.specs/time-programs.md): its per-switch-time entities, its poll, and the write
// path. The files of this component's root are in every build, so this code compiles only when the entity platforms
// it needs are in the build; the hub's time_programs config loads them.

#include "esphome/core/defines.h"

#if defined(USE_DATETIME_TIME) && defined(USE_SELECT) && defined(USE_SWITCH)

#include <array>
#include <cstdint>
#include <string>

#include "esphome/components/datetime/time_entity.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif
#ifdef USE_EVENT
#include "esphome/components/event/event.h"
#endif

#include "sec_touch.h"
#include "time_program_codec.h"
#include "time_program_ids.h"
#include "time_program_mask.h"
#include "time_program_write.h"

namespace esphome {
namespace sec_touch {

class TimeProgramTime;
class TimeProgramLevelSelect;
class TimeProgramEnabledSwitch;

/**
 * One fan pair's time program. Its entities attach themselves; each attach puts that field's ID on the pair's poll
 * and routes its data messages here. Writes go out one SET per ID, then a read-back GET of that ID; the enabled mask
 * is read fresh right before it is written, so only the requested bits change.
 */
class TimeProgram {
 public:
  TimeProgram(SECTouchComponent *hub, int pair);

  int get_pair() const { return this->pair_; }

  void attach_time(int switch_time, TimeProgramTime *entity);
  void attach_level(int switch_time, TimeProgramLevelSelect *entity);
  void attach_enabled(int switch_time, TimeProgramEnabledSwitch *entity);
#ifdef USE_EVENT
  void set_write_failed_event(event::Event *event) { this->write_failed_ = event; }
#endif

  void write_time(int switch_time, uint8_t hour, uint8_t minute);
  void write_level(int switch_time, int level);
  void write_enabled(int switch_time, bool enabled);
  /// Queues one poll of this pair; the regular poll stays on its schedule.
  void refresh();

 protected:
  /// What a write sent, kept until the read-back. `armed` from the SET's end until the next GET of the ID ends.
  struct ReadBack {
    int expected{-1};
    bool armed{false};
  };

  /// Puts `id` on the pair's poll and routes its data messages here, once per ID.
  void watch_(int id);
  void on_value_(int id, const std::string &raw);
  void on_time_(int switch_time, const std::string &raw);
  void on_level_(int switch_time, const std::string &raw);
  void on_mask_(const std::string &raw);
  void on_task_end_(const BaseTask &task, TaskEnd end);
  /// The read-back slot of a time-program ID of this pair, or nullptr.
  ReadBack *read_back_(int id);
  /// Compares a read-back with what was sent; `actual` is -1 for a value the meaning rule marks unexpected.
  void check_read_back_(int id, int actual);
  void queue_set_(int id, const char *value, int expected);
  void write_mask_(uint8_t fresh);
  void fail_mask_write_(const char *why);
  /// Publishes each listed switch time's enabled switch from `mask` whose state differs.
  void publish_switches_(uint8_t mask);
  void report_(int id, WriteFailure failure);

  SECTouchComponent *hub_;
  int pair_;
  std::array<TimeProgramTime *, SWITCH_TIME_COUNT> times_{};
  std::array<TimeProgramLevelSelect *, SWITCH_TIME_COUNT> levels_{};
  std::array<TimeProgramEnabledSwitch *, SWITCH_TIME_COUNT> switches_{};
#ifdef USE_EVENT
  event::Event *write_failed_{nullptr};
#endif

  std::array<ReadBack, SWITCH_TIME_COUNT> time_read_backs_{};
  std::array<ReadBack, SWITCH_TIME_COUNT> level_read_backs_{};
  ReadBack mask_read_back_{};
  /// Which IDs watch_() has routed here, by offset in the pair's block (enabled, times, levels).
  std::array<bool, TIME_PROGRAM_IDS_PER_PAIR> watched_{};

  MaskMerge mask_merge_;
  /// A mask GET is wanted: the next fresh mask applies the requested bits.
  bool mask_read_pending_{false};
  int last_good_mask_{-1};
};

class TimeProgramTime : public datetime::TimeEntity {
 public:
  TimeProgramTime(TimeProgram *program, int switch_time);
  /// Publishes a time read from the panel.
  void publish_time(uint8_t hour, uint8_t minute, uint8_t second);
  /// Leaves the entity with no state, as after an unexpected value.
  void clear_state();

 protected:
  void control(const datetime::TimeCall &call) override;

  TimeProgram *program_;
  int switch_time_;
};

class TimeProgramLevelSelect : public select::Select {
 public:
  TimeProgramLevelSelect(TimeProgram *program, int switch_time);
  void clear_state();

 protected:
  void control(size_t index) override;

  TimeProgram *program_;
  int switch_time_;
};

class TimeProgramEnabledSwitch : public switch_::Switch {
 public:
  TimeProgramEnabledSwitch(TimeProgram *program, int switch_time);

 protected:
  void write_state(bool state) override;

  TimeProgram *program_;
  int switch_time_;
};

#ifdef USE_BUTTON
class TimeProgramRefreshButton : public button::Button {
 public:
  explicit TimeProgramRefreshButton(TimeProgram *program) : program_(program) {}

 protected:
  void press_action() override { this->program_->refresh(); }

  TimeProgram *program_;
};
#endif

#ifdef USE_EVENT
/// The pair's `write_failed` event: its types are WRITE_FAILURE_NAMES.
class TimeProgramWriteFailedEvent : public event::Event {
 public:
  explicit TimeProgramWriteFailedEvent(TimeProgram *program);
};
#endif

}  // namespace sec_touch
}  // namespace esphome

#endif
