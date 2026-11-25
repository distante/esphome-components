#pragma once

// A fan pair's time program in HA (.specs/time-programs.md): its per-switch-time entities, its poll, and the write
// path. The files of this component's root are in every build, so this code compiles only when the entity platforms
// it needs are in the build; the hub's time_programs config loads them.

#include "esphome/core/defines.h"

#if defined(USE_DATETIME_TIME) && defined(USE_SELECT) && defined(USE_SWITCH)

#include <array>
#include <cstdint>
#include <optional>
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
#include "time_program_bulk.h"
#include "time_program_codec.h"
#include "time_program_ids.h"
#include "time_program_mask.h"
#include "time_program_write.h"

namespace esphome {
namespace sec_touch {

class TimeProgramTime;
class TimeProgramLevelSelect;
class TimeProgramEnabledSwitch;

/// Hears how a bulk write (TimeProgram::write_program) ended.
class BulkWriteListener {
 public:
  /// Every write of fan pair `pair`'s bulk write has ended; `error` is the report when one failed.
  virtual void on_bulk_write_done(int pair, const std::optional<std::string> &error) = 0;

 protected:
  ~BulkWriteListener() = default;
};

/// Logs why `sec_touch.set_time_program` wrote nothing for fan pair `pair`.
void log_bulk_write_refused(int pair, const std::string &error);

/**
 * One fan pair's time program. Its entities attach themselves; each attach puts that field's ID on the pair's poll.
 * The data messages of the pair's whole block come here. Writes go out one SET per ID, then a read-back GET of that ID;
 * the enabled mask is read fresh right before it is written, so only the requested bits change.
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

  /// The entity writes. Each one replaces the field's value of a running bulk write.
  void write_time(int switch_time, uint8_t hour, uint8_t minute);
  void write_level(int switch_time, int level);
  void write_enabled(int switch_time, bool enabled);
  /// Queues one poll of this pair; the regular poll stays on its schedule.
  void refresh();

  /// A bulk write of this pair runs: a new one would be `busy`.
  bool is_bulk_write_running() const { return this->bulk_.active(); }
  /**
   * Writes the whole `program` through the per-field write path: the 5 times, the 5 levels, then the mask from a fresh
   * GET. Tells `listener` once every write has ended. Call it only while no bulk write runs.
   */
  void write_program(const BulkProgram &program, BulkWriteListener *listener);

 protected:
  /// What a write sent, kept until the read-back. `armed` from the SET's end until the next GET of the ID ends.
  struct ReadBack {
    int expected{-1};
    bool armed{false};
  };

  /// Puts `id` on the pair's poll, once per ID.
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
  void queue_time_(int switch_time, uint8_t hour, uint8_t minute);
  void queue_level_(int switch_time, int level);
  /// Requests switch time `switch_time`'s bit; the mask is written from a fresh GET.
  void request_enabled_(int switch_time, bool enabled);
  void queue_set_(int id, const char *value, int expected);
  void write_mask_(uint8_t fresh);
  void fail_mask_write_(const char *why);
  /// Publishes each listed switch time's enabled switch from `mask` whose state differs.
  void publish_switches_(uint8_t mask);
  void report_(int id, WriteFailure failure);
  /// Ends the bulk write once each of its writes has ended, and tells its listener.
  void finish_bulk_write_if_done_();

  SECTouchComponent *hub_;
  int pair_;
  std::array<TimeProgramTime *, SWITCH_TIME_COUNT> times_{};
  std::array<TimeProgramLevelSelect *, SWITCH_TIME_COUNT> levels_{};
  std::array<TimeProgramEnabledSwitch *, SWITCH_TIME_COUNT> switches_{};
#ifdef USE_EVENT
  event::Event *write_failed_{nullptr};
#endif

  /// By time_program_block_index().
  std::array<ReadBack, TIME_PROGRAM_IDS_PER_PAIR> read_backs_{};
  /// Which IDs watch_() has put on the poll, by time_program_block_index().
  std::array<bool, TIME_PROGRAM_IDS_PER_PAIR> watched_{};

  MaskMerge mask_merge_;
  /// A mask GET is wanted: the next fresh mask applies the requested bits.
  bool mask_read_pending_{false};
  int last_good_mask_{-1};

  BulkWrite bulk_;
  BulkWriteListener *bulk_listener_{nullptr};
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
