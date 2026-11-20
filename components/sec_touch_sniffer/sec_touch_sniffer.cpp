#include "sec_touch_sniffer.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "sniffer_state.h"

namespace esphome {
namespace sec_touch {

/// The fewest bytes around a value in a frame the hub routes: STX, the TABs after the command and property IDs (both
/// may be empty), and ETX, with no CRC field.
static constexpr size_t MIN_DATA_FRAME_OVERHEAD = 4;
// The hub keeps one byte of its frame buffer unused, so a frame is at most sizeof(buffer) - 1 bytes.
static_assert(RAW_VALUE_CAP >= sizeof(IncomingMessage::buffer) - 1 - MIN_DATA_FRAME_OVERHEAD,
              "RAW_VALUE_CAP must hold every value the hub's frame buffer can deliver");

/// Wait after a scan query's first timeout before its one retry.
static constexpr uint32_t SCAN_RETRY_DELAY_MS = 5000;
/// Gap between one scan query's outcome and the next query.
static constexpr uint32_t SCAN_NEXT_DELAY_MS = 160;

const char *const SecTouchSniffer::TAG = "sec_touch_sniffer";

static uint32_t uptime_s() { return millis() / 1000; }

SecTouchSniffer::SecTouchSniffer(SECTouchComponent *parent) : parent_(parent) {}

void SecTouchSniffer::setup() {
  if (!this->store_.allocate(this->web_page_)) {
    ESP_LOGE(TAG, "Not enough heap for the property records");
    this->mark_failed();
    return;
  }
  this->state_.reserve(MAX_STATE_LEN);

  this->parent_->register_message_tap([this](int command_id, int property_id, const std::string &raw_value) {
    this->on_message_(command_id, property_id, raw_value);
  });

  this->parent_->register_queue_empty_listener([this]() { this->on_queue_empty_(); });

  this->update_scan_switch_();
}

void SecTouchSniffer::dump_config() {
  ESP_LOGCONFIG(TAG, "SEC-Touch-Sniffer:");
  if (this->scan_end_ > 0) {
    ESP_LOGCONFIG(TAG, "  Scan range: %d - %d", this->scan_start_, this->scan_end_);
  } else {
    ESP_LOGCONFIG(TAG, "  Scan: not configured (passive mode only)");
  }
  ESP_LOGCONFIG(TAG, "  Web page: %s", YESNO(this->web_page_));
  ESP_LOGCONFIG(TAG, "  History: %s", this->store_.history().is_allocated() ? "on" : "off");
}

void SecTouchSniffer::update_scan_switch_() {
  if (this->scan_switch_ != nullptr) {
    this->scan_switch_->publish_state(this->is_scanning_);
  }
}

void SecTouchSniffer::publish_records_() {
  build_sniffer_state(this->store_, [this](int id) { return this->parent_->is_property_registered(id); }, this->state_);
  this->publish_state(this->state_);
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
    this->pending_query_id_ = -1;
    this->is_scanning_ = false;
    this->update_scan_switch_();
    this->publish_records_();
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
  this->pending_query_id_ = -1;
  this->is_scanning_ = true;
  this->update_scan_switch_();
  this->parent_->enter_scan_mode();
  // First task is submitted by on_queue_empty_() which fires immediately because
  // enter_scan_mode() cleared the queue and reset the running task state
}

void SecTouchSniffer::query_(int property_id) {
  ESP_LOGD(TAG, "Scan: queuing GET for property_id %d", property_id);
  this->pending_query_id_ = property_id;
  this->parent_->add_discovery_get_task(property_id);
}

void SecTouchSniffer::on_queue_empty_() {
  if (!this->is_scanning_) {
    return;
  }

  if (this->pending_query_id_ >= 0) {
    const int queried = this->pending_query_id_;
    this->pending_query_id_ = -1;
    const ScanOutcome outcome = this->parent_->get_last_scan_outcome();
    if (outcome == ScanOutcome::SCAN_OUTCOME_TIMEOUT && !this->scan_retry_pending_) {
      ESP_LOGW(TAG, "Scan: property_id %d timed out, retrying in %" PRIu32 "s", queried, SCAN_RETRY_DELAY_MS / 1000);
      this->scan_retry_pending_ = true;
      // Rewound, so a scan stopped during the wait resumes at this ID.
      this->current_scan_id_ = queried;
      this->set_timeout("scan_retry", SCAN_RETRY_DELAY_MS, [this]() {
        if (!this->is_scanning_) {
          return;
        }
        this->query_(this->current_scan_id_++);
      });
      return;
    }
    if (outcome == ScanOutcome::SCAN_OUTCOME_TIMEOUT) {
      ESP_LOGW(TAG, "Scan: property_id %d timed out again, skipping", queried);
    }
    this->scan_retry_pending_ = false;
    this->store_.record_query(queried, outcome, uptime_s());
  }

  while (this->current_scan_id_ <= this->scan_end_ && this->parent_->is_property_registered(this->current_scan_id_)) {
    ESP_LOGI(TAG, "Scan: skipping registered property_id %d", this->current_scan_id_);
    this->current_scan_id_++;
  }

  if (this->current_scan_id_ > this->scan_end_) {
    ESP_LOGI(TAG, "Scan complete.");
    this->is_scanning_ = false;
    this->update_scan_switch_();
    this->publish_records_();
    this->parent_->exit_scan_mode();
    return;
  }
  int id_to_query = this->current_scan_id_++;
  this->set_timeout("scan_next", SCAN_NEXT_DELAY_MS, [this, id_to_query]() {
    if (!this->is_scanning_) {
      return;
    }
    this->query_(id_to_query);
  });
}

void SecTouchSniffer::on_message_(int command_id, int property_id, const std::string &raw_value) {
  const uint32_t now_s = uptime_s();
  const RecordResult result =
      this->store_.record_message(command_id, property_id, raw_value.data(), raw_value.size(), now_s);

  if (result == RecordResult::RECORD_DROPPED) {
    if (this->store_.dropped() == 1) {
      ESP_LOGW(TAG, "property_id %d has no record slot (0-%d); such messages are dropped and counted", property_id,
               MAX_PROPERTY_ID);
    }
    return;
  }
  if (result == RecordResult::RECORD_REJECTED) {
    ESP_LOGW(TAG, "property_id %d: raw value of %zu bytes not recorded (cap %zu)", property_id, raw_value.size(),
             RAW_VALUE_CAP);
    return;
  }
  if (result == RecordResult::RECORD_UNCHANGED) {
    return;
  }

  if (result == RecordResult::RECORD_NEW) {
    ESP_LOGI(TAG, "NEW property_id %d (cmd=%d raw='%s' at=%" PRIu32 "s)", property_id, command_id, raw_value.c_str(),
             now_s);
  } else {
    ESP_LOGI(TAG, "property_id %d changed (cmd=%d raw='%s' at=%" PRIu32 "s, change %" PRIu32 ")", property_id,
             command_id, raw_value.c_str(), now_s, this->store_.fields(property_id).change_count);
  }

  if (this->is_scanning_ || !is_listed_in_state(this->store_, property_id,
                                                [this](int id) { return this->parent_->is_property_registered(id); })) {
    return;
  }
  this->publish_records_();
}

}  // namespace sec_touch
}  // namespace esphome
