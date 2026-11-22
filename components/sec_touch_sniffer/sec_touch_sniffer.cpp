#include "sec_touch_sniffer.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "json_chunk_writer.h"

#ifdef USE_ESP32
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
  if (!this->store_.allocate(/*with_history=*/true)) {
    ESP_LOGE(TAG, "Not enough heap for the property records");
    this->mark_failed();
    return;
  }

  this->parent_->register_message_tap([this](int command_id, int property_id, const std::string &raw_value) {
    this->on_message_(command_id, property_id, raw_value);
  });

  this->parent_->register_queue_empty_listener([this]() { this->on_queue_empty_(); });

  this->disable_loop();

#ifdef USE_ESP32
  get_mac_address_into_buffer(this->mac_);
  this->new_boot_id_();
  web_server_base::global_web_server_base->init();
  web_server_base::global_web_server_base->add_handler(this);
#endif
}

void SecTouchSniffer::dump_config() {
  ESP_LOGCONFIG(TAG, "SEC-Touch-Sniffer:");
  ESP_LOGCONFIG(TAG, "  History: %s", this->store_.history().is_allocated() ? "on" : "off");
}

void SecTouchSniffer::request_scan(bool start, const ScanRun &run) {
  this->scan_request_.store(
      PendingScanRequest{start ? ScanRequest::SCAN_REQUEST_START : ScanRequest::SCAN_REQUEST_STOP, run});
  this->enable_loop_soon_any_context();
}

void SecTouchSniffer::loop() {
  // Disabled before the request is taken, so a request queued after this point enables the loop again.
  this->disable_loop();
  const PendingScanRequest request = this->scan_request_.exchange(NO_SCAN_REQUEST);
  if (request.kind == ScanRequest::SCAN_REQUEST_START) {
    this->start_scan(request.run);
    return;
  }
  if (request.kind == ScanRequest::SCAN_REQUEST_STOP) {
    this->stop_scan();
  }
}

void SecTouchSniffer::start_scan(const ScanRun &run) {
  if (this->is_scanning_) {
    return;
  }
  if (run.goal == ScanGoal::SCAN_GOAL_CHECK) {
    ESP_LOGI(TAG, "Scan mode ON — check: every known property ID");
  } else {
    ESP_LOGI(TAG, "Scan mode ON — discover: IDs %d to %d, known IDs skipped", run.from, run.to);
  }
  // No resume: every run begins at its goal's first ID.
  this->scan_goal_ = run.goal;
  this->scan_from_ = run.from;
  this->scan_to_ = run.to;
  this->current_scan_id_ = first_scan_id(run);
  this->scan_completed_ = false;
  this->scan_ran_ = true;
  this->scan_retry_pending_ = false;
  this->pending_query_id_ = NO_PENDING_QUERY;
  this->is_scanning_ = true;
  this->parent_->enter_scan_mode();
  // First task is submitted by on_queue_empty_() which fires immediately because
  // enter_scan_mode() cleared the queue and reset the running task state
}

void SecTouchSniffer::stop_scan() {
  if (!this->is_scanning_) {
    return;
  }
  ESP_LOGI(TAG, "Scan mode OFF — stopped");
  this->end_scan_(/*completed=*/false);
}

void SecTouchSniffer::end_scan_(bool completed) {
  this->cancel_timeout("scan_retry");
  this->cancel_timeout("scan_next");
  this->scan_retry_pending_ = false;
  this->pending_query_id_ = NO_PENDING_QUERY;
  this->scan_completed_ = completed;
  this->is_scanning_ = false;
  this->parent_->exit_scan_mode();
}

int SecTouchSniffer::take_next_scan_id_() {
  const int id = this->current_scan_id_;
  this->current_scan_id_ = next_scan_id(this->scan_run_(), id + 1);
  return id;
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
      // Back at the timed-out ID for the wait, so the page's `next` names the ID the retry queries.
      this->current_scan_id_ = queried;
      this->set_timeout("scan_retry", SCAN_RETRY_DELAY_MS, [this]() {
        if (!this->is_scanning_) {
          return;
        }
        this->query_(this->take_next_scan_id_());
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

  if (this->current_scan_id_ == NO_SCAN_ID) {
    ESP_LOGI(TAG, "Scan complete.");
    this->end_scan_(/*completed=*/true);
    return;
  }
  // current_scan_id_ stays at the ID for the wait, so the page's `next` names it until the query is queued.
  this->set_timeout("scan_next", SCAN_NEXT_DELAY_MS, [this]() {
    if (!this->is_scanning_) {
      return;
    }
    this->query_(this->take_next_scan_id_());
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
    return;
  }
  ESP_LOGI(TAG, "property_id %d changed (cmd=%d raw='%s' at=%" PRIu32 "s, change %" PRIu32 ")", property_id, command_id,
           raw_value.c_str(), now_s, this->store_.fields(property_id).change_count);
}

#ifdef USE_ESP32

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
      this->serve_page_(request);
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
    case SnifferRoute::ROUTE_CLEAR:
      this->serve_clear_(*request);
      return;
    case SnifferRoute::ROUTE_NONE:
      return;
  }
}

// Straight from flash, as web_server serves its own page.
void SecTouchSniffer::serve_page_(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response =
      request->beginResponse(200, "text/html", SEC_TOUCH_SNIFFER_PAGE, SEC_TOUCH_SNIFFER_PAGE_SIZE);
  response->addHeader("Content-Encoding", "gzip");
  request->send(response);
}

bool SecTouchSniffer::send_chunk_(void *request, const char *data, size_t len) {
  return httpd_resp_send_chunk(static_cast<httpd_req_t *>(request), data, len) == ESP_OK;
}

// Chunked on the raw request: the server's response objects buffer the whole body. So the status and headers,
// Access-Control-Allow-Origin included, are set here.
void SecTouchSniffer::begin_json_response_(httpd_req_t *req, const char *status) {
  httpd_resp_set_status(req, status);
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
      this->json_, this->store_, device, scan_state(this->scan_fields_()), since_from_uri(req->uri),
      this->record_scratch_,
      [this](auto read) {
        LockGuard guard(this->store_lock_);
        read();
      },
      this->is_registered_());
  this->end_json_response_(req);
}

void SecTouchSniffer::serve_scan_(httpd_req_t *req, bool start) {
  ScanRun run = DEFAULT_DISCOVER_RUN;
  if (start) {
    const ScanStart parsed = scan_start_from_uri(req->uri);
    if (!parsed.valid) {
      // Nothing starts.
      this->begin_json_response_(req, HTTPD_400);
      this->json_.raw(R"({"error":"from and to must both be given, 0-)");
      this->json_.number(MAX_PROPERTY_ID);
      this->json_.raw(R"(, with from <= to"})");
      this->end_json_response_(req);
      return;
    }
    run = parsed.run;
  }
  const ScanFields fields = this->scan_fields_();
  this->request_scan(start, run);
  this->begin_json_response_(req);
  write_scan_json(this->json_, scan_reply(fields, start, run));
  this->end_json_response_(req);
}

void SecTouchSniffer::new_boot_id_() { format_hex_to(this->boot_, random_uint32()); }

void SecTouchSniffer::serve_clear_(httpd_req_t *req) {
  {
    LockGuard guard(this->store_lock_);
    this->store_.clear();
  }
  // Only the httpd task reads boot_, in serve_data_(), so no lock. Every open page sees the boot change and resyncs.
  this->new_boot_id_();
  ESP_LOGI(TAG, "Records cleared by the page");
  this->begin_json_response_(req);
  this->json_.raw("{}");
  this->end_json_response_(req);
}

#endif  // USE_ESP32

}  // namespace sec_touch
}  // namespace esphome
