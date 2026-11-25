#include "time_program.h"

#if defined(USE_DATETIME_TIME) && defined(USE_SELECT) && defined(USE_SWITCH)

#include "esphome/core/controller_registry.h"
#include "esphome/core/log.h"

namespace esphome {
namespace sec_touch {

static const char *const TAG = "sec_touch.time_program";

namespace {

/// Where `slot` sits in its pair's block: the enabled mask, then the times, then the levels.
std::size_t block_index(const TimeProgramSlot &slot) {
  switch (slot.field) {
    case TimeProgramField::ENABLED:
      return 0;
    case TimeProgramField::TIME:
      return slot.switch_time;
    case TimeProgramField::LEVEL:
      return SWITCH_TIME_COUNT + slot.switch_time;
  }
  return 0;
}

}  // namespace

TimeProgram::TimeProgram(SECTouchComponent *hub, int pair) : hub_(hub), pair_(pair) {
  this->hub_->register_time_program(pair, this);
  // The whole block's data messages come here, listed switch time or not: a bulk write reads back every field. Only
  // the polled IDs (watch_()) and the read-backs ever answer.
  auto on_value = [this](int property_id, const std::string &raw) { this->on_value_(property_id, raw); };
  this->hub_->register_time_program_listener(time_program_enabled_id(pair), on_value);
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    this->hub_->register_time_program_listener(time_program_time_id(pair, n), on_value);
    this->hub_->register_time_program_listener(time_program_level_id(pair, n), on_value);
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
  bool &watched = this->watched_[block_index(*slot)];
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

void TimeProgram::write_time(int switch_time, uint8_t hour, uint8_t minute) {
  const int id = time_program_time_id(this->pair_, switch_time);
  SwitchTimeTimeText text = format_switch_time_time(hour, minute);
  ESP_LOGI(TAG, "Fan pair %d switch time %d time -> %s", this->pair_, switch_time, text.data());
  this->queue_set_(id, text.data(), minute_of_day(hour, minute));
}

void TimeProgram::write_level(int switch_time, int level) {
  const int id = time_program_level_id(this->pair_, switch_time);
  SwitchTimeLevelText text = format_switch_time_level(level);
  ESP_LOGI(TAG, "Fan pair %d switch time %d level -> %s", this->pair_, switch_time, text.data());
  this->queue_set_(id, text.data(), level);
}

void TimeProgram::write_enabled(int switch_time, bool enabled) {
  ESP_LOGI(TAG, "Fan pair %d switch time %d enabled -> %s", this->pair_, switch_time, enabled ? "on" : "off");
  this->mask_merge_.request(switch_time, enabled);
  if (this->mask_read_pending_) {
    return;
  }
  // Read the mask fresh right before writing it, so bits changed on the panel since the last poll stay as they are.
  this->mask_read_pending_ =
      this->hub_->add_priority_get_task(TaskTargetType::TIME_PROGRAM, time_program_enabled_id(this->pair_));
}

void TimeProgram::queue_set_(int id, const char *value, int expected) {
  ReadBack *read_back = this->read_back_(id);
  if (read_back == nullptr) {
    return;
  }
  read_back->expected = expected;
  this->hub_->add_set_task_latest_wins(TaskTargetType::TIME_PROGRAM, id, value);
}

TimeProgram::ReadBack *TimeProgram::read_back_(int id) {
  auto slot = time_program_slot(id);
  if (!slot.has_value() || slot->pair != this->pair_) {
    return nullptr;
  }
  switch (slot->field) {
    case TimeProgramField::ENABLED:
      return &this->mask_read_back_;
    case TimeProgramField::TIME:
      return &this->time_read_backs_[slot->switch_time - 1];
    case TimeProgramField::LEVEL:
      return &this->level_read_backs_[slot->switch_time - 1];
  }
  return nullptr;
}

void TimeProgram::check_read_back_(int id, int actual) {
  auto slot = time_program_slot(id);
  if (slot.has_value()) {
    this->bulk_.read_back(*slot, actual);
  }
  ReadBack *read_back = this->read_back_(id);
  if (read_back == nullptr || !read_back->armed) {
    return;
  }
  read_back->armed = false;
  if (actual != read_back->expected) {
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
    if (this->mask_read_pending_) {
      this->fail_mask_write_("its fresh read was unexpected");
    }
    return;
  }
  this->last_good_mask_ = mask;
  if (this->mask_read_pending_) {
    this->write_mask_(static_cast<uint8_t>(mask));
    return;
  }
  this->publish_switches_(static_cast<uint8_t>(mask));
}

void TimeProgram::write_mask_(uint8_t fresh) {
  const uint8_t mask = this->mask_merge_.apply(fresh);
  this->mask_merge_.clear();
  this->mask_read_pending_ = false;
  EnabledMaskText text = format_enabled_mask(mask);
  ESP_LOGI(TAG, "Fan pair %d enabled mask -> %s", this->pair_, text.data());
  this->queue_set_(time_program_enabled_id(this->pair_), text.data(), mask);
  this->publish_switches_(mask);
}

void TimeProgram::fail_mask_write_(const char *why) {
  ESP_LOGW(TAG, "Fan pair %d enabled mask: nothing written, %s", this->pair_, why);
  this->mask_merge_.clear();
  this->mask_read_pending_ = false;
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
    // In scan mode a read-back would never reach this program: data messages go to the sniffer only.
    if (this->hub_->is_scan_mode_active()) {
      this->bulk_.set_ended(*slot, false);
      return;
    }
    read_back->armed = true;
    this->bulk_.set_ended(*slot, this->hub_->add_priority_get_task(TaskTargetType::TIME_PROGRAM, task.property_id));
    return;
  }

  // A GET of this ID ended. A delivered answer already took the read-back; a failed GET leaves nothing to compare.
  read_back->armed = false;
  this->bulk_.get_ended(*slot, end);
  if (slot->field != TimeProgramField::ENABLED || !this->mask_read_pending_) {
    return;
  }
  if (end != TaskEnd::TASK_END_ANSWERED) {
    this->fail_mask_write_(task_end_name(end));
    return;
  }
  if (this->hub_->is_scan_mode_active()) {
    this->fail_mask_write_("its fresh read went to the sniffer");
  }
}

void log_bulk_write_refused(int pair, const std::string &error) {
  ESP_LOGW(TAG, "Fan pair %d: set_time_program wrote nothing, %s", pair, error.c_str());
}

void TimeProgram::write_program(const BulkProgram &program, BulkWriteListener *listener) {
  ESP_LOGI(TAG, "Fan pair %d: set_time_program writes the whole program", this->pair_);
  this->bulk_.begin(program);
  this->bulk_listener_ = listener;
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    const SwitchTimeTime &time = program.times[n - 1];
    this->write_time(n, time.hour, time.minute);
  }
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    this->write_level(n, program.levels[n - 1]);
  }
  // Each switch time's bit goes through the mask merge: one mask SET built from a fresh GET.
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    this->write_enabled(n, (program.enabled & switch_time_bit(n)) != 0);
  }
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
  this->bulk_.fail(*slot, failure);
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
  const uint8_t hour = call.get_hour().value_or(this->hour_);
  const uint8_t minute = call.get_minute().value_or(this->minute_);
  this->program_->write_time(this->switch_time_, hour, minute);
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
  this->program_->write_level(this->switch_time_, static_cast<int>(index));
  this->publish_state(index);
}

TimeProgramEnabledSwitch::TimeProgramEnabledSwitch(TimeProgram *program, int switch_time)
    : program_(program), switch_time_(switch_time) {
  this->program_->attach_enabled(switch_time, this);
}

void TimeProgramEnabledSwitch::write_state(bool state) {
  this->program_->write_enabled(this->switch_time_, state);
  this->publish_state(state);
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
