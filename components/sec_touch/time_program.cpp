#include "time_program.h"

#if defined(USE_DATETIME_TIME) && defined(USE_SELECT) && defined(USE_SWITCH)

#include "esphome/core/controller_registry.h"
#include "esphome/core/log.h"

#include <utility>

namespace esphome {
namespace sec_touch {

static const char *const TAG = "sec_touch.time_program";

TimeProgram::TimeProgram(SECTouchComponent *hub, int pair) : hub_(hub), pair_(pair) {
  this->hub_->register_time_program(pair, this);
  // The whole block's data messages come here, listed switch time or not: a bulk write reads back every field. Only
  // the polled IDs (watch_()) and the read-backs ever answer.
  auto on_value = [this](int property_id, const std::string &raw) { this->on_value_(property_id, raw); };
  this->hub_->register_unpolled_string_listener(time_program_enabled_id(pair), on_value);
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    this->hub_->register_unpolled_string_listener(time_program_time_id(pair, n), on_value);
    this->hub_->register_unpolled_string_listener(time_program_level_id(pair, n), on_value);
  }
  // Every step of a bulk write ends with a task end: a delivered answer reaches on_value_() just before it.
  this->hub_->register_task_end_listener([this](const BaseTask &task, TaskEnd end) {
    this->on_task_end_(task, end);
    this->finish_bulk_write_if_done_();
  });
}

void TimeProgram::attach_time(int switch_time, TimeProgramTime *entity) {
  if (!is_switch_time(switch_time)) {
    ESP_LOGE(TAG, "Fan pair %d has no switch time %d", this->pair_, switch_time);
    return;
  }
  this->times_[switch_time - 1] = entity;
  this->watch_(time_program_time_id(this->pair_, switch_time));
}

void TimeProgram::attach_level(int switch_time, TimeProgramLevelSelect *entity) {
  if (!is_switch_time(switch_time)) {
    ESP_LOGE(TAG, "Fan pair %d has no switch time %d", this->pair_, switch_time);
    return;
  }
  this->levels_[switch_time - 1] = entity;
  this->watch_(time_program_level_id(this->pair_, switch_time));
}

void TimeProgram::attach_enabled(int switch_time, TimeProgramEnabledSwitch *entity) {
  if (!is_switch_time(switch_time)) {
    ESP_LOGE(TAG, "Fan pair %d has no switch time %d", this->pair_, switch_time);
    return;
  }
  this->switches_[switch_time - 1] = entity;
  this->watch_(time_program_enabled_id(this->pair_));
}

void TimeProgram::watch_(int id) {
  auto slot = time_program_slot(id);
  if (!slot.has_value() || slot->pair != this->pair_) {
    ESP_LOGE(TAG, "Property ID %d is not part of fan pair %d's time program", id, this->pair_);
    return;
  }
  bool &watched = this->watched_[time_program_block_index(*slot)];
  if (watched) {
    return;
  }
  watched = true;
  this->hub_->add_time_program_poll_id(this->pair_, id);
}

void TimeProgram::refresh() {
  ESP_LOGD(TAG, "Refreshing fan pair %d's time program", this->pair_);
  this->hub_->add_time_program_tasks_to_queue(this->pair_);
}

bool TimeProgram::write_time(int switch_time, uint8_t hour, uint8_t minute) {
  if (!is_time_of_day(hour, minute)) {
    ESP_LOGE(TAG, "Fan pair %d switch time %d time: %d:%d is no time of day, nothing written", this->pair_, switch_time,
             hour, minute);
    return false;
  }
  this->bulk_.replaced(TimeProgramSlot{this->pair_, switch_time, TimeProgramField::TIME});
  this->queue_time_(switch_time, hour, minute);
  return true;
}

bool TimeProgram::write_level(int switch_time, int level) {
  if (!is_switch_time_level(level)) {
    ESP_LOGE(TAG, "Fan pair %d switch time %d level: %d is no switch time level, nothing written", this->pair_,
             switch_time, level);
    return false;
  }
  this->bulk_.replaced(TimeProgramSlot{this->pair_, switch_time, TimeProgramField::LEVEL});
  this->queue_level_(switch_time, level);
  return true;
}

void TimeProgram::write_enabled(int switch_time, bool enabled) {
  this->bulk_.replaced(TimeProgramSlot{this->pair_, 0, TimeProgramField::ENABLED});
  this->request_enabled_(switch_time, enabled);
  this->read_mask_fresh_();
}

void TimeProgram::queue_time_(int switch_time, uint8_t hour, uint8_t minute) {
  const int id = time_program_time_id(this->pair_, switch_time);
  SwitchTimeTimeText text = format_switch_time_time(hour, minute);
  ESP_LOGI(TAG, "Fan pair %d switch time %d time -> %s", this->pair_, switch_time, text.data());
  this->queue_set_(id, text.data(), minute_of_day(hour, minute));
}

void TimeProgram::queue_level_(int switch_time, int level) {
  const int id = time_program_level_id(this->pair_, switch_time);
  SwitchTimeLevelText text = format_switch_time_level(level);
  ESP_LOGI(TAG, "Fan pair %d switch time %d level -> %s", this->pair_, switch_time, text.data());
  this->queue_set_(id, text.data(), level);
}

void TimeProgram::request_enabled_(int switch_time, bool enabled) {
  ESP_LOGI(TAG, "Fan pair %d switch time %d enabled -> %s", this->pair_, switch_time, enabled ? "on" : "off");
  this->mask_merge_.request(switch_time, enabled);
}

void TimeProgram::read_mask_fresh_() {
  if (this->mask_read_pending_) {
    return;
  }
  // Read the mask fresh right before writing it, so bits changed on the panel since the last poll stay as they are.
  this->mask_get_ticket_ =
      this->hub_->add_priority_get_task(TaskTargetType::TIME_PROGRAM, time_program_enabled_id(this->pair_));
  if (this->mask_get_ticket_ == 0) {
    this->fail_mask_write_("no GET could be queued");
    return;
  }
  this->mask_read_pending_ = true;
}

void TimeProgram::queue_set_(int id, const char *value, int expected) {
  auto slot = time_program_slot(id);
  ReadBack *read_back = this->read_back_(id);
  if (!slot.has_value() || read_back == nullptr) {
    return;
  }
  read_back->expected = expected;
  read_back->set_ticket = this->hub_->add_set_task_latest_wins(TaskTargetType::TIME_PROGRAM, id, value);
  if (read_back->set_ticket != 0) {
    return;
  }
  // No SET was queued, so no task end comes for it: the write ends here.
  this->bulk_.fail(*slot, WriteFailure::TIMEOUT);
  this->bulk_.set_ended(*slot, false);
  this->report_(id, WriteFailure::TIMEOUT);
}

TimeProgram::ReadBack *TimeProgram::read_back_(int id) {
  auto slot = time_program_slot(id);
  if (!slot.has_value() || slot->pair != this->pair_) {
    return nullptr;
  }
  return &this->read_backs_[time_program_block_index(*slot)];
}

void TimeProgram::check_read_back_(int id, int actual) {
  auto slot = time_program_slot(id);
  ReadBack *read_back = this->read_back_(id);
  if (!slot.has_value() || read_back == nullptr) {
    return;
  }
  // The one comparison of a read-back with what was sent, for the entity write and the bulk write alike.
  const bool matched = actual == read_back->expected;
  this->bulk_.read_back(*slot, matched);
  if (!read_back->armed) {
    return;
  }
  read_back->armed = false;
  if (!matched && !read_back->reported) {
    this->report_(id, WriteFailure::NOT_KEPT);
  }
}

void TimeProgram::on_value_(int id, const std::string &raw) {
  auto slot = time_program_slot(id);
  if (!slot.has_value() || slot->pair != this->pair_) {
    return;
  }
  switch (slot->field) {
    case TimeProgramField::ENABLED:
      this->on_mask_(raw);
      return;
    case TimeProgramField::TIME:
      this->on_time_(slot->switch_time, raw);
      return;
    case TimeProgramField::LEVEL:
      this->on_level_(slot->switch_time, raw);
      return;
  }
}

void TimeProgram::on_time_(int switch_time, const std::string &raw) {
  const int id = time_program_time_id(this->pair_, switch_time);
  // A newer SET of this ID is still queued: this answer is older than it and would flip the entity back.
  if (this->hub_->has_pending_set_task_for(id)) {
    ESP_LOGD(TAG, "Skipping stale echo for property_id %d — newer SET pending", id);
    return;
  }
  auto time = parse_switch_time_time(raw);
  this->check_read_back_(id, time.has_value() ? minute_of_day(time->hour, time->minute) : -1);
  TimeProgramTime *entity = this->times_[switch_time - 1];
  if (!time.has_value()) {
    ESP_LOGW(TAG, "Fan pair %d switch time %d time: unexpected value '%s'", this->pair_, switch_time, raw.c_str());
    if (entity != nullptr) {
      entity->clear_state();
    }
    return;
  }
  if (entity != nullptr) {
    entity->publish_time(time->hour, time->minute, time->second);
  }
}

void TimeProgram::on_level_(int switch_time, const std::string &raw) {
  const int id = time_program_level_id(this->pair_, switch_time);
  if (this->hub_->has_pending_set_task_for(id)) {
    ESP_LOGD(TAG, "Skipping stale echo for property_id %d — newer SET pending", id);
    return;
  }
  const int level = parse_switch_time_level(raw);
  this->check_read_back_(id, level);
  TimeProgramLevelSelect *entity = this->levels_[switch_time - 1];
  if (level < 0) {
    ESP_LOGW(TAG, "Fan pair %d switch time %d level: unexpected value '%s'", this->pair_, switch_time, raw.c_str());
    if (entity != nullptr) {
      entity->clear_state();
    }
    return;
  }
  if (entity == nullptr) {
    return;
  }
  if (entity->has_state() && entity->active_index() == static_cast<size_t>(level)) {
    return;
  }
  entity->publish_state(static_cast<size_t>(level));
}

void TimeProgram::on_mask_(const std::string &raw) {
  const int id = time_program_enabled_id(this->pair_);
  // A mask SET is still queued: this answer is older than it. A pending bit request waits for the mask GET queued
  // behind that SET.
  if (this->hub_->has_pending_set_task_for(id)) {
    ESP_LOGD(TAG, "Skipping stale echo for property_id %d — newer SET pending", id);
    return;
  }
  const int mask = parse_enabled_mask(raw);
  this->check_read_back_(id, mask);
  if (mask < 0) {
    ESP_LOGW(TAG, "Fan pair %d enabled mask: unexpected value '%s'", this->pair_, raw.c_str());
  } else {
    this->last_good_mask_ = mask;
  }
  // A pending bit request waits for the task end: only the answer of the write's own GET is its fresh mask.
  if (this->mask_read_pending_) {
    this->mask_answer_ = mask;
    return;
  }
  if (mask >= 0) {
    this->publish_switches_(static_cast<uint8_t>(mask));
  }
}

void TimeProgram::write_mask_(uint8_t fresh) {
  const uint8_t mask = this->mask_merge_.apply(fresh);
  this->mask_merge_.clear();
  this->mask_read_pending_ = false;
  EnabledMaskText text = format_enabled_mask(mask);
  ESP_LOGI(TAG, "Fan pair %d enabled mask -> %s", this->pair_, text.data());
  // A running bulk write requested its bits before this fresh mask: this SET is its mask SET, so a SET that can't be
  // queued fails it.
  this->bulk_.mask_set_queued();
  this->queue_set_(time_program_enabled_id(this->pair_), text.data(), mask);
  this->publish_switches_(mask);
}

void TimeProgram::fail_mask_write_(const char *why) {
  ESP_LOGW(TAG, "Fan pair %d enabled mask: nothing written, %s", this->pair_, why);
  this->mask_merge_.clear();
  this->mask_read_pending_ = false;
  this->bulk_.fail(TimeProgramSlot{this->pair_, 0, TimeProgramField::ENABLED}, WriteFailure::MASK_UNREADABLE);
  this->report_(time_program_enabled_id(this->pair_), WriteFailure::MASK_UNREADABLE);
  if (this->last_good_mask_ >= 0) {
    this->publish_switches_(static_cast<uint8_t>(this->last_good_mask_));
  }
}

void TimeProgram::publish_switches_(uint8_t mask) {
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    TimeProgramEnabledSwitch *entity = this->switches_[n - 1];
    if (entity == nullptr) {
      continue;
    }
    const bool enabled = (mask & switch_time_bit(n)) != 0;
    if (entity->has_state() && entity->state == enabled) {
      continue;
    }
    entity->publish_state(enabled);
  }
}

void TimeProgram::on_task_end_(const BaseTask &task, TaskEnd end) {
  // A delivered answer comes right before the end of the task it answers, so it is this task's or no one's.
  const std::optional<int> mask_answer = std::exchange(this->mask_answer_, std::nullopt);
  if (task.targetType != TaskTargetType::TIME_PROGRAM) {
    return;
  }
  auto slot = time_program_slot(task.property_id);
  if (!slot.has_value() || slot->pair != this->pair_) {
    return;
  }
  ReadBack *read_back = this->read_back_(task.property_id);

  if (task.get_task_type() == TaskType::SET_DATA) {
    auto failure = set_write_failure(end);
    if (failure.has_value()) {
      this->report_(task.property_id, *failure);
    }
    // A SET that was in flight when a newer SET of the ID was queued only reports: the newer one is the bulk write's
    // and gets the read-back.
    if (task.ticket != read_back->set_ticket) {
      return;
    }
    read_back->reported = failure.has_value();
    if (failure.has_value()) {
      this->bulk_.fail(*slot, *failure);
    }
    // A dropped SET gets no read-back: it was never sent, or a scan started or stopped while it was in flight. A SET
    // that ends during a scan gets one: writes stay allowed then, so its GET does too.
    if (task.dropped) {
      this->bulk_.set_ended(*slot, false);
      return;
    }
    read_back->armed = true;
    const uint32_t read_back_ticket = this->hub_->add_priority_get_task(TaskTargetType::TIME_PROGRAM, task.property_id);
    // No read-back GET, nothing to compare: the bulk write fails.
    if (read_back_ticket == 0) {
      this->bulk_.fail(*slot, WriteFailure::TIMEOUT);
    }
    this->bulk_.set_ended(*slot, read_back_ticket != 0);
    return;
  }

  // A GET of this ID ended. A delivered answer already took the read-back; a failed GET leaves nothing to compare.
  read_back->armed = false;
  this->bulk_.get_ended(*slot, end);
  // Only the switch write's own fresh GET decides that the mask is unreadable, not a poll's GET of it.
  if (slot->field != TimeProgramField::ENABLED || !this->mask_read_pending_ || task.ticket != this->mask_get_ticket_) {
    return;
  }
  if (end != TaskEnd::TASK_END_ANSWERED) {
    this->fail_mask_write_(task_end_name(end));
    return;
  }
  if (this->hub_->is_scan_mode_active()) {
    this->fail_mask_write_("its fresh read went to the sniffer");
    return;
  }
  if (!mask_answer.has_value()) {
    this->fail_mask_write_("its fresh read brought no mask");
    return;
  }
  if (*mask_answer < 0) {
    this->fail_mask_write_("its fresh read was unexpected");
    return;
  }
  this->write_mask_(static_cast<uint8_t>(*mask_answer));
}

void log_bulk_write_refused(int pair, const std::string &error) {
  ESP_LOGW(TAG, "Fan pair %d: set_time_program wrote nothing, %s", pair, error.c_str());
}

void TimeProgram::write_program(const BulkProgram &program, BulkWriteListener *listener) {
  ESP_LOGI(TAG, "Fan pair %d: set_time_program writes the whole program", this->pair_);
  this->bulk_.begin();
  this->bulk_listener_ = listener;
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    const SwitchTimeTime &time = program.times[n - 1];
    this->queue_time_(n, time.hour, time.minute);
  }
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    this->queue_level_(n, program.levels[n - 1]);
  }
  // Each switch time's bit goes through the mask merge: one mask SET built from a fresh GET.
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    this->request_enabled_(n, (program.enabled & switch_time_bit(n)) != 0);
  }
  this->read_mask_fresh_();
}

void TimeProgram::finish_bulk_write_if_done_() {
  if (!this->bulk_.done()) {
    return;
  }
  BulkWriteListener *listener = this->bulk_listener_;
  this->bulk_listener_ = nullptr;
  // finish() frees the pair before the listener runs: its automation may start the next bulk write.
  const std::optional<std::string> error = this->bulk_.finish();
  if (error.has_value()) {
    ESP_LOGW(TAG, "Fan pair %d: set_time_program: %s", this->pair_, error->c_str());
  } else {
    ESP_LOGI(TAG, "Fan pair %d: set_time_program: every write landed", this->pair_);
  }
  if (listener != nullptr) {
    listener->on_bulk_write_done(this->pair_, error);
  }
}

void TimeProgram::report_(int id, WriteFailure failure) {
  auto slot = time_program_slot(id);
  if (!slot.has_value()) {
    return;
  }
  if (slot->field == TimeProgramField::ENABLED) {
    ESP_LOGW(TAG, "Fan pair %d %s: write failed, %s", this->pair_, time_program_field_name(slot->field),
             write_failure_name(failure));
  } else {
    ESP_LOGW(TAG, "Fan pair %d switch time %d %s: write failed, %s", this->pair_, slot->switch_time,
             time_program_field_name(slot->field), write_failure_name(failure));
  }
#ifdef USE_EVENT
  if (this->write_failed_ != nullptr) {
    this->write_failed_->trigger(write_failure_name(failure));
  }
#endif
}

TimeProgramTime::TimeProgramTime(TimeProgram *program, int switch_time) : program_(program), switch_time_(switch_time) {
  this->program_->attach_time(switch_time, this);
}

void TimeProgramTime::publish_time(uint8_t hour, uint8_t minute, uint8_t second) {
  if (this->has_state() && this->hour_ == hour && this->minute_ == minute && this->second_ == second) {
    return;
  }
  this->hour_ = hour;
  this->minute_ = minute;
  this->second_ = second;
  this->publish_state();
}

void TimeProgramTime::clear_state() {
  this->set_has_state(false);
#if defined(USE_CONTROLLER_REGISTRY)
  ControllerRegistry::notify_time_update(this);
#endif
}

void TimeProgramTime::control(const datetime::TimeCall &call) {
  // A call may give only the hour or only the minute: the other one is the one shown, so without a state there is none.
  if ((!call.get_hour().has_value() || !call.get_minute().has_value()) && !this->has_state()) {
    ESP_LOGW(TAG, "Fan pair %d switch time %d time: shows no time, so a call needs both hour and minute",
             this->program_->get_pair(), this->switch_time_);
    return;
  }
  const uint8_t hour = call.get_hour().value_or(this->hour_);
  const uint8_t minute = call.get_minute().value_or(this->minute_);
  if (!this->program_->write_time(this->switch_time_, hour, minute)) {
    return;
  }
  // The panel's seconds are always 00: the written time shows as it lands.
  this->hour_ = hour;
  this->minute_ = minute;
  this->second_ = 0;
  this->publish_state();
}

TimeProgramLevelSelect::TimeProgramLevelSelect(TimeProgram *program, int switch_time)
    : program_(program), switch_time_(switch_time) {
  this->traits.set_options_static(SWITCH_TIME_LEVEL_OPTIONS.data(), SWITCH_TIME_LEVEL_OPTIONS.size());
  this->program_->attach_level(switch_time, this);
}

void TimeProgramLevelSelect::clear_state() {
  this->set_has_state(false);
#if defined(USE_CONTROLLER_REGISTRY)
  ControllerRegistry::notify_select_update(this);
#endif
}

void TimeProgramLevelSelect::control(size_t index) {
  // Option index = level (SWITCH_TIME_LEVEL_OPTIONS).
  if (!this->program_->write_level(this->switch_time_, static_cast<int>(index))) {
    return;
  }
  this->publish_state(index);
}

TimeProgramEnabledSwitch::TimeProgramEnabledSwitch(TimeProgram *program, int switch_time)
    : program_(program), switch_time_(switch_time) {
  this->program_->attach_enabled(switch_time, this);
}

void TimeProgramEnabledSwitch::write_state(bool state) {
  // Shown first: a write that writes nothing shows the last good mask again over it.
  this->publish_state(state);
  this->program_->write_enabled(this->switch_time_, state);
}

#ifdef USE_EVENT
TimeProgramWriteFailedEvent::TimeProgramWriteFailedEvent(TimeProgram *program) {
  this->set_event_types_static(WRITE_FAILURE_NAMES.data(), WRITE_FAILURE_NAMES.size());
  program->set_write_failed_event(this);
}
#endif

}  // namespace sec_touch
}  // namespace esphome

#endif
