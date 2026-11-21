#include "sec_touch_sniffer.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "json_chunk_writer.h"
#include "sniffer_state.h"

#ifdef USE_SEC_TOUCH_SNIFFER_WEB_PAGE
#include "esphome/core/application.h"
#endif

namespace esphome {
namespace sec_touch {

const char *const SecTouchSniffer::TAG = "sec_touch_sniffer";

/// Device uptime in seconds. From the 64-bit millis_64(): 32-bit millis() wraps after 49.7 days, seconds after 136
/// years.
static uint32_t uptime_s() { return static_cast<uint32_t>(millis_64() / MS_PER_S); }

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
  this->disable_loop();

#ifdef USE_SEC_TOUCH_SNIFFER_WEB_PAGE
  get_mac_address_into_buffer(this->mac_);
  format_hex_to(this->boot_, random_uint32());
  web_server_base::global_web_server_base->init();
  web_server_base::global_web_server_base->add_handler(this);
#endif
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

void SecTouchSniffer::request_scan(bool start) {
  if (this->scan_end_ == 0) {
    return;
  }
  this->scan_request_.store(start ? ScanRequest::SCAN_REQUEST_START : ScanRequest::SCAN_REQUEST_STOP);
  this->enable_loop_soon_any_context();
}

void SecTouchSniffer::loop() {
  // Disabled before the request is taken, so a request queued after this point enables the loop again.
  this->disable_loop();
  const ScanRequest request = this->scan_request_.exchange(ScanRequest::SCAN_REQUEST_NONE);
  if (request == ScanRequest::SCAN_REQUEST_NONE) {
    return;
  }
  const bool start = request == ScanRequest::SCAN_REQUEST_START;
  if (start == this->is_scanning_) {
    return;
  }
  this->toggle_scan();
}

void SecTouchSniffer::update_scan_switch_() {
  if (this->scan_switch_ != nullptr) {
    this->scan_switch_->publish_state(this->is_scanning_);
  }
}

void SecTouchSniffer::publish_records_() {
  build_sniffer_state(this->store_, this->is_registered_(), this->state_);
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
    this->pending_query_id_ = NO_PENDING_QUERY;
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

  this->current_scan_id_ = scan_resume_id(this->scan_start_, this->scan_end_, this->current_scan_id_);

  ESP_LOGI(TAG, "Scan mode ON — scanning IDs %d to %d (resuming from %d)", this->scan_start_, this->scan_end_,
           this->current_scan_id_.load());
  this->pending_query_id_ = NO_PENDING_QUERY;
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

  if (this->pending_query_id_ != NO_PENDING_QUERY) {
    const int queried = this->pending_query_id_;
    this->pending_query_id_ = NO_PENDING_QUERY;
    const ScanOutcome outcome = this->parent_->get_last_scan_outcome();
    if (outcome == ScanOutcome::SCAN_OUTCOME_TIMEOUT && !this->scan_retry_pending_) {
      ESP_LOGW(TAG, "Scan: property_id %d timed out, retrying in %" PRIu32 "s", queried,
               SCAN_RETRY_DELAY_MS / MS_PER_S);
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
    LockGuard guard(this->store_lock_);
    this->store_.record_query(queried, outcome, uptime_s());
  }

  while (this->current_scan_id_ <= this->scan_end_ && this->parent_->is_property_registered(this->current_scan_id_)) {
    ESP_LOGI(TAG, "Scan: skipping registered property_id %d", this->current_scan_id_.load());
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
  RecordResult result;
  {
    LockGuard guard(this->store_lock_);
    result = this->store_.record_message(command_id, property_id, raw_value, now_s);
  }

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

  if (this->is_scanning_ || !is_listed_in_state(this->store_, property_id, this->is_registered_())) {
    return;
  }
  this->publish_records_();
}

#ifdef USE_SEC_TOUCH_SNIFFER_WEB_PAGE

/// Until the page lands (issue #35).
static constexpr char PLACEHOLDER_PAGE[] = "<!doctype html><meta charset=utf-8><title>SEC-Touch sniffer</title>"
                                           "<p>The sniffer page is not built yet.";

static SnifferRoute route_of(AsyncWebServerRequest *request) {
  SnifferMethod method = SnifferMethod::METHOD_OTHER;
  if (request->method() == HTTP_GET) {
    method = SnifferMethod::METHOD_GET;
  } else if (request->method() == HTTP_POST) {
    method = SnifferMethod::METHOD_POST;
  }
  if (method == SnifferMethod::METHOD_OTHER) {
    return SnifferRoute::ROUTE_NONE;
  }
  char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
  const StringRef url = request->url_to(url_buf);
  return sniffer_route(method, std::string_view(url.c_str(), url.size()));
}

bool SecTouchSniffer::canHandle(AsyncWebServerRequest *request) const {
  return route_of(request) != SnifferRoute::ROUTE_NONE;
}

void SecTouchSniffer::handleRequest(AsyncWebServerRequest *request) {
  switch (route_of(request)) {
    case SnifferRoute::ROUTE_PAGE:
      request->send(200, "text/html", PLACEHOLDER_PAGE);
      return;
    case SnifferRoute::ROUTE_DATA:
      this->serve_data_(*request);
      return;
    case SnifferRoute::ROUTE_SCAN_START:
      this->serve_scan_(*request, /*start=*/true);
      return;
    case SnifferRoute::ROUTE_SCAN_STOP:
      this->serve_scan_(*request, /*start=*/false);
      return;
    case SnifferRoute::ROUTE_NONE:
      return;
  }
}

bool SecTouchSniffer::send_chunk_(void *request, const char *data, size_t len) {
  return httpd_resp_send_chunk(static_cast<httpd_req_t *>(request), data, len) == ESP_OK;
}

// Chunked on the raw request: the server's response objects buffer the whole body. So the status and headers,
// Access-Control-Allow-Origin included, are set here.
void SecTouchSniffer::begin_json_response_(httpd_req_t *req) {
  httpd_resp_set_status(req, HTTPD_200);
  httpd_resp_set_type(req, "application/json");
  // Mirrors web_server_base's DefaultHeaders, which the raw response bypasses; its guard enforces allowed_origins.
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  this->json_.begin(req);
}

void SecTouchSniffer::end_json_response_(httpd_req_t *req) {
  if (!this->json_.flush()) {
    ESP_LOGW(TAG, "Sending %s failed", req->uri);
    return;
  }
  httpd_resp_send_chunk(req, nullptr, 0);
}

void SecTouchSniffer::serve_data_(httpd_req_t *req) {
  const StringRef name = App.get_name();
  const DeviceInfo device{
      .mac = this->mac_,
      .name = std::string_view(name.c_str(), name.size()),
      .boot = this->boot_,
      .uptime_ms = millis_64(),
  };
  this->begin_json_response_(req);
  write_data_json(
      this->json_, this->store_, device, this->scan_state_(), since_from_uri(req->uri), this->record_scratch_,
      [this](auto read) {
        LockGuard guard(this->store_lock_);
        read();
      },
      this->is_registered_());
  this->end_json_response_(req);
}

void SecTouchSniffer::serve_scan_(httpd_req_t *req, bool start) {
  this->request_scan(start);
  this->begin_json_response_(req);
  write_scan_json(this->json_,
                  scan_reply(this->scan_start_, this->scan_end_, this->current_scan_id_, this->is_scanning_, start));
  this->end_json_response_(req);
}

#endif  // USE_SEC_TOUCH_SNIFFER_WEB_PAGE

}  // namespace sec_touch
}  // namespace esphome
