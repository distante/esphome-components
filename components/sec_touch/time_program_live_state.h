#pragma once

// A fan pair's live-state entities (.specs/time-programs.md, "Live state"): which switch time is in effect, the
// running level, the next level and the next change. The logic is in time_program_live_model.h; this class feeds it
// the hub's data messages and publishes what it computes.

#include "esphome/core/defines.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif

#include "sec_touch.h"
#include "time_program_live_model.h"

namespace esphome {
namespace sec_touch {

/**
 * One fan pair's live state, shared by the pair's live-state entities and its fan when the fan follows the running
 * level (which pairs get one: `_prepare_live_states` in __init__.py). It puts the pair's whole block on the
 * pair's poll and ID 2 (the panel clock) on the global-settings poll, recomputes on every input read and at the next
 * change, and publishes an entity only when its value changes.
 */
class TimeProgramLiveState : public Component {
 public:
  TimeProgramLiveState(SECTouchComponent *hub, int pair);

  void set_switch_time_in_effect(text_sensor::TextSensor *sensor) { this->in_effect_ = sensor; }
  void set_running_level(text_sensor::TextSensor *sensor) { this->running_level_ = sensor; }
  void set_next_level(text_sensor::TextSensor *sensor) { this->next_level_ = sensor; }
#ifdef USE_TIME
  void set_next_change(text_sensor::TextSensor *sensor, time::RealTimeClock *time) {
    this->next_change_ = sensor;
    this->time_ = time;
  }
#endif
  /// Hands every state the model computes to `listener`, nullopt while a needed input was never read well. One per
  /// pair: the pair's fan.
  void set_state_listener(std::function<void(const std::optional<LiveState> &)> listener) {
    this->state_listener_ = std::move(listener);
  }

  void loop() override;
  void dump_config() override;

 protected:
  void on_block_value_(int id, const std::string &raw);
  void on_pair_level_(const std::string &raw);
  void on_clock_(const std::string &raw);
  /// Whether a SET of `id` is still queued: a GET answer older than it would show the old value.
  bool is_stale_echo_(int id) const;
  void recompute_();
  void publish_text_(text_sensor::TextSensor *sensor, const char *text);
  void publish_next_change_(const LiveState &state);
  /// A needed input was never read well: each entity with a state goes back to none (unknown), as at boot.
  void clear_entities_();
  void clear_text_(text_sensor::TextSensor *sensor);

  SECTouchComponent *hub_;
  int pair_;
  LiveStateModel model_;

  text_sensor::TextSensor *in_effect_{nullptr};
  text_sensor::TextSensor *running_level_{nullptr};
  text_sensor::TextSensor *next_level_{nullptr};
#ifdef USE_TIME
  text_sensor::TextSensor *next_change_{nullptr};
  time::RealTimeClock *time_{nullptr};
#endif
  std::function<void(const std::optional<LiveState> &)> state_listener_;

  /// The one timeout: at the next change, recompute. millis() of the change, valid while `next_change_armed_`.
  uint32_t next_change_at_ms_{0};
  bool next_change_armed_{false};
  /// The next change was known but the time source wasn't synced: recompute once it is.
  bool next_change_waits_for_time_{false};
};

}  // namespace sec_touch
}  // namespace esphome
