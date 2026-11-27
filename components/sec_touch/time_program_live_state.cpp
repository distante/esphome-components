#include "time_program_live_state.h"

#include "esphome/core/controller_registry.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace sec_touch {

static const char *const TAG = "sec_touch.live_state";

TimeProgramLiveState::TimeProgramLiveState(SECTouchComponent *hub, int pair) : hub_(hub), pair_(pair), model_(pair) {
  if (!is_time_program_pair(pair)) {
    ESP_LOGE(TAG, "Fan pair %d has no time program", pair);
    return;
  }
  // The whole block, whatever switch_times lists: every switch time feeds the model.
  auto watch = [this](int id) {
    this->hub_->add_time_program_poll_id(this->pair_, id);
    this->hub_->register_unpolled_string_listener(
        id, [this](int property_id, const std::string &raw) { this->on_block_value_(property_id, raw); });
  };
  watch(time_program_enabled_id(pair));
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    watch(time_program_time_id(pair, n));
  }
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    watch(time_program_level_id(pair, n));
  }
  // The pair's fan entry, which the config requires, polls its level ID; listen only.
  this->hub_->register_unpolled_string_listener(FAN_LEVEL_IDS[pair - 1],
                                                [this](int, const std::string &raw) { this->on_pair_level_(raw); });
  // Only the panel clock: the panel date is clock_drift's.
  this->hub_->register_global_setting_string_listener(CLOCK_PROPERTY_ID,
                                                      [this](int, const std::string &raw) { this->on_clock_(raw); });
}

void TimeProgramLiveState::loop() {
#ifdef USE_TIME
  if (this->next_change_waits_for_time_ && this->time_->now().is_valid()) {
    this->recompute_();
    return;
  }
#endif
  // The wait is at most a day, far below what millis_reached() allows.
  if (this->next_change_armed_ && millis_reached(millis(), this->next_change_at_ms_)) {
    this->recompute_();
  }
}

void TimeProgramLiveState::dump_config() {
  ESP_LOGCONFIG(TAG, "Time program live state, fan pair %d", this->pair_);
  LOG_TEXT_SENSOR("  ", "Switch time in effect", this->in_effect_);
  LOG_TEXT_SENSOR("  ", "Running level", this->running_level_);
  LOG_TEXT_SENSOR("  ", "Next level", this->next_level_);
#ifdef USE_TIME
  LOG_TEXT_SENSOR("  ", "Next change", this->next_change_);
#endif
}

bool TimeProgramLiveState::is_stale_echo_(int id) const {
  if (!this->hub_->has_pending_set_task_for(id)) {
    return false;
  }
  ESP_LOGD(TAG, "Skipping stale echo for property_id %d — newer SET pending", id);
  return true;
}

void TimeProgramLiveState::on_block_value_(int id, const std::string &raw) {
  auto slot = time_program_slot(id);
  if (!slot.has_value() || slot->pair != this->pair_ || this->is_stale_echo_(id)) {
    return;
  }
  if (!this->model_.take_block_value(*slot, raw)) {
    ESP_LOGW(TAG, "Fan pair %d property_id %d: unexpected value '%s', keeping the last good one", this->pair_, id,
             raw.c_str());
  }
  this->recompute_();
}

void TimeProgramLiveState::on_pair_level_(const std::string &raw) {
  if (this->is_stale_echo_(FAN_LEVEL_IDS[this->pair_ - 1])) {
    return;
  }
  if (!this->model_.take_pair_level(raw)) {
    ESP_LOGW(TAG, "Fan pair %d level: unexpected value '%s', keeping the last good one", this->pair_, raw.c_str());
  }
  this->recompute_();
}

void TimeProgramLiveState::on_clock_(const std::string &raw) {
  if (!this->model_.take_clock(raw, millis())) {
    ESP_LOGW(TAG, "Panel clock: unexpected value '%s', keeping the last good one", raw.c_str());
  }
  this->recompute_();
}

void TimeProgramLiveState::recompute_() {
  const uint32_t now_ms = millis();
  auto state = this->model_.compute(now_ms);
  this->next_change_armed_ = false;
  this->next_change_waits_for_time_ = false;
  if (!state.has_value()) {
    this->clear_entities_();
    return;
  }
  if (state->ms_to_next_change.has_value()) {
    this->next_change_at_ms_ = now_ms + *state->ms_to_next_change;
    this->next_change_armed_ = true;
  }
  this->publish_text_(this->in_effect_, state->in_effect);
  this->publish_text_(this->running_level_, state->running_level);
  this->publish_text_(this->next_level_, state->next_level);
  this->publish_next_change_(*state);
}

void TimeProgramLiveState::publish_text_(text_sensor::TextSensor *sensor, const char *text) {
  if (sensor == nullptr) {
    return;
  }
  if (sensor->has_state() && sensor->get_state() == text) {
    return;
  }
  sensor->publish_state(text);
}

void TimeProgramLiveState::clear_entities_() {
  this->clear_text_(this->in_effect_);
  this->clear_text_(this->running_level_);
  this->clear_text_(this->next_level_);
#ifdef USE_TIME
  this->clear_text_(this->next_change_);
#endif
}

void TimeProgramLiveState::clear_text_(text_sensor::TextSensor *sensor) {
  if (sensor == nullptr || !sensor->has_state()) {
    return;
  }
  sensor->set_has_state(false);
#if defined(USE_TEXT_SENSOR) && defined(USE_CONTROLLER_REGISTRY)
  ControllerRegistry::notify_text_sensor_update(sensor);
#endif
}

void TimeProgramLiveState::publish_next_change_(const LiveState &state) {
#ifdef USE_TIME
  if (this->next_change_ == nullptr) {
    return;
  }
  if (!state.ms_to_next_change.has_value()) {
    this->clear_text_(this->next_change_);
    return;
  }
  if (!this->time_->now().is_valid()) {
    ESP_LOGD(TAG, "Time source not synced yet, next change waits");
    this->next_change_waits_for_time_ = true;
    return;
  }
  const auto iso = next_change_iso(next_change_timestamp(this->time_->timestamp_now(), *state.ms_to_next_change));
  if (iso.text[0] == '\0') {
    ESP_LOGW(TAG, "Next change does not fit an ISO time, next change unknown");
    this->clear_text_(this->next_change_);
    return;
  }
  this->publish_text_(this->next_change_, iso.text.data());
#else
  (void) state;
#endif
}

}  // namespace sec_touch
}  // namespace esphome
