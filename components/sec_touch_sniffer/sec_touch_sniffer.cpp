#include "sec_touch_sniffer.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace sec_touch {

const char *const SecTouchSniffer::TAG = "sec_touch_sniffer";

SecTouchSniffer::SecTouchSniffer(SECTouchComponent *parent) : parent_(parent) {}

void SecTouchSniffer::setup() {
  this->parent_->register_raw_message_listener([this](int command_id, int property_id, int new_value) {
    this->on_raw_message_(command_id, property_id, new_value);
  });

  this->parent_->register_queue_empty_listener([this]() { this->on_queue_empty_(); });

  this->update_scan_switch_();
}

void SecTouchSniffer::dump_config() {
  ESP_LOGCONFIG(TAG, "SEC-Touch-Sniffer:");
  ESP_LOGCONFIG(TAG, "  Time source: %s", this->time_ != nullptr ? "RTC" : "uptime fallback");
  if (this->scan_end_ > 0) {
    ESP_LOGCONFIG(TAG, "  Scan range: %d - %d", this->scan_start_, this->scan_end_);
  } else {
    ESP_LOGCONFIG(TAG, "  Scan: not configured (passive mode only)");
  }
  if (this->discovered_ids_.empty()) {
    ESP_LOGCONFIG(TAG, "  Discovered IDs: none");
  } else {
    ESP_LOGCONFIG(TAG, "  Discovered IDs (%d): %s", (int) this->discovered_ids_.size(),
                  this->build_state_string_().c_str());
  }
}

void SecTouchSniffer::update_scan_switch_() {
  if (this->scan_switch_ != nullptr) {
    this->scan_switch_->publish_state(this->is_scanning_);
  }
}

void SecTouchSniffer::toggle_scan() {
  if (this->is_scanning_) {
    if (this->current_scan_id_ > this->scan_start_) {
      ESP_LOGI(TAG, "Scan mode OFF — last queued ID: %d", this->current_scan_id_ - 1);
    } else {
      ESP_LOGI(TAG, "Scan mode OFF — no IDs were queued");
    }
    this->cancel_timeout("scan_retry");
    this->cancel_timeout("scan_next");
    this->scan_retry_pending_ = false;
    this->is_scanning_ = false;
    this->update_scan_switch_();
    if (!this->discovered_ids_.empty()) {
      this->publish_state(this->build_state_string_());
    }
    this->parent_->exit_scan_mode();
    return;
  }

  if (this->scan_end_ == 0) {
    ESP_LOGW(TAG, "toggle_scan() called but no scan range configured — ignoring");
    this->is_scanning_ = false;
    this->update_scan_switch_();
    return;
  }

  if (this->current_scan_id_ == 0 || this->current_scan_id_ > this->scan_end_) {
    this->current_scan_id_ = this->scan_start_;
  }

  ESP_LOGI(TAG, "Scan mode ON — scanning IDs %d to %d (resuming from %d)", this->scan_start_, this->scan_end_,
           this->current_scan_id_);
  this->is_scanning_ = true;
  this->update_scan_switch_();
  this->parent_->enter_scan_mode();
  // First task is submitted by on_queue_empty_() which fires immediately because
  // enter_scan_mode() cleared the queue and reset the running task state
}

void SecTouchSniffer::on_queue_empty_() {
  if (!this->is_scanning_) {
    return;
  }

  if (this->parent_->get_last_scan_task_timed_out()) {
    int failed_id = this->current_scan_id_ - 1;
    if (this->scan_retry_pending_) {
      ESP_LOGW(TAG, "Scan: property_id %d timed out again, skipping", failed_id);
      this->scan_retry_pending_ = false;
    } else {
      ESP_LOGW(TAG, "Scan: property_id %d timed out, retrying in 5s", failed_id);
      this->scan_retry_pending_ = true;
      this->current_scan_id_--;
      this->set_timeout("scan_retry", 5000, [this]() {
        if (!this->is_scanning_) {
          return;
        }
        ESP_LOGD(TAG, "Scan: queuing retry GET for property_id %d", this->current_scan_id_);
        this->parent_->add_discovery_get_task(this->current_scan_id_++);
      });
      return;
    }
  } else {
    this->scan_retry_pending_ = false;
  }

  while (this->current_scan_id_ <= this->scan_end_ && this->parent_->is_property_registered(this->current_scan_id_)) {
    ESP_LOGI(TAG, "Scan: skipping registered property_id %d", this->current_scan_id_);
    this->current_scan_id_++;
  }

  if (this->current_scan_id_ > this->scan_end_) {
    ESP_LOGI(TAG, "Scan complete. %d unique IDs discovered.", this->discovered_ids_.size());
    this->is_scanning_ = false;
    this->update_scan_switch_();
    this->publish_state(this->build_state_string_());
    this->parent_->exit_scan_mode();
    return;
  }
  int id_to_query = this->current_scan_id_++;
  this->set_timeout("scan_next", 160, [this, id_to_query]() {
    if (!this->is_scanning_) {
      return;
    }
    ESP_LOGD(TAG, "Scan: queuing GET for property_id %d", id_to_query);
    this->parent_->add_discovery_get_task(id_to_query);
  });
}

void SecTouchSniffer::on_raw_message_(int command_id, int property_id, int new_value) {
  auto it = this->discovered_ids_.find(property_id);
  bool is_new = (it == this->discovered_ids_.end());
  bool changed = is_new || (it->second.value != new_value || it->second.command_id != command_id);

  if (!changed) {
    return;
  }

  std::string ts = this->get_timestamp_();
  SniffedEntry &entry = this->discovered_ids_[property_id];
  entry.command_id = command_id;
  entry.value = new_value;
  std::strncpy(entry.last_seen_at, ts.c_str(), sizeof(entry.last_seen_at) - 1);
  entry.last_seen_at[sizeof(entry.last_seen_at) - 1] = '\0';

  if (is_new) {
    ESP_LOGI(TAG, "NEW property_id %d (cmd=%d value=%d at=%s) total=%d", property_id, command_id, new_value, ts.c_str(),
             this->discovered_ids_.size());
  } else {
    ESP_LOGD(TAG, "property_id %d updated (cmd=%d value=%d at=%s)", property_id, command_id, new_value, ts.c_str());
  }

  if (!this->is_scanning_) {
    this->publish_state(this->build_state_string_());
  }
}

std::string SecTouchSniffer::get_timestamp_() const {
  if (this->time_ != nullptr) {
    auto now = this->time_->now();
    if (now.is_valid()) {
      return now.strftime("%d-%m-%Y %H:%M");
    }
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "t=%lus", millis() / 1000UL);
  return std::string(buf);
}

std::string SecTouchSniffer::build_state_string_() const {
  char buf[MAX_STATE_LEN + 4];  // +4 for worst-case ", ..." suffix during truncation
  size_t pos = 0;
  bool first = true;

  for (const auto &kv : this->discovered_ids_) {
    char entry[32];
    int written = snprintf(entry, sizeof(entry), "%d=%d", kv.first, kv.second.value);
    if (written <= 0 || written >= (int) sizeof(entry)) {
      continue;
    }
    size_t entry_len = (size_t) written;
    size_t sep_len = first ? 0 : 2;

    if (pos + sep_len + entry_len + 3 > MAX_STATE_LEN) {
      if (!first) {
        memcpy(buf + pos, ", ", 2);
        pos += 2;
      }
      memcpy(buf + pos, "...", 3);
      pos += 3;
      break;
    }

    if (!first) {
      memcpy(buf + pos, ", ", 2);
      pos += 2;
    }
    memcpy(buf + pos, entry, entry_len);
    pos += entry_len;
    first = false;
  }

  buf[pos] = '\0';
  return std::string(buf, pos);
}

}  // namespace sec_touch
}  // namespace esphome
