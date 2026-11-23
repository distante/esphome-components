#include "sec_touch_sniffer.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
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

// --- The run slot ---

bool SecTouchSniffer::request_run_(const PendingRun &run) {
  {
    LockGuard guard(this->run_lock_);
    if (this->run_claimed_) {
      return false;
    }
    this->run_claimed_ = true;
    this->pending_ = run;
  }
  this->enable_loop_soon_any_context();
  return true;
}

void SecTouchSniffer::release_run_() {
  LockGuard guard(this->run_lock_);
  this->run_claimed_ = false;
  this->stop_pending_ = false;
}

bool SecTouchSniffer::request_scan_start(const ScanRun &run) {
  PendingRun pending{};
  pending.kind = PendingRun::Kind::PENDING_SCAN;
  pending.scan = run;
  return this->request_run_(pending);
}

bool SecTouchSniffer::request_probe(ProbeKind kind, uint8_t id, const ProbeValue &value) {
  PendingRun pending{};
  pending.kind = PendingRun::Kind::PENDING_PROBE;
  pending.probe_kind = kind;
  pending.probe_id = id;
  pending.probe_value = value;
  return this->request_run_(pending);
}

void SecTouchSniffer::request_scan_stop() {
  {
    LockGuard guard(this->run_lock_);
    if (!this->is_scanning_ && this->pending_.kind != PendingRun::Kind::PENDING_SCAN) {
      return;
    }
    this->stop_pending_ = true;
  }
  this->enable_loop_soon_any_context();
}

void SecTouchSniffer::loop() {
  // Disabled before the requests are taken, so a request queued after this point enables the loop again.
  this->disable_loop();
  PendingRun pending{};
  bool stop = false;
  {
    LockGuard guard(this->run_lock_);
    pending = this->pending_;
    this->pending_.kind = PendingRun::Kind::PENDING_NONE;
    stop = this->stop_pending_;
    this->stop_pending_ = false;
  }
  if (pending.kind == PendingRun::Kind::PENDING_SCAN) {
    this->begin_scan_(pending.scan);
  } else if (pending.kind == PendingRun::Kind::PENDING_PROBE) {
    this->begin_probe_(pending.probe_kind, pending.probe_id, pending.probe_value);
  }
  // After the start: a stop queued for a scan that was still waiting stops it now.
  if (stop) {
    this->stop_scan();
  }
}

ScanFields SecTouchSniffer::scan_fields_() {
  ScanRun run = DEFAULT_DISCOVER_RUN;
  {
    LockGuard guard(this->run_lock_);
    run = this->scan_run_;
  }
  return ScanFields{.ran = this->scan_ran_,
                    .active = this->is_scanning_,
                    .completed = this->scan_completed_,
                    .run = run,
                    .current = this->current_scan_id_};
}

// --- Scans ---

bool SecTouchSniffer::start_scan(const ScanRun &run) {
  {
    LockGuard guard(this->run_lock_);
    if (this->run_claimed_) {
      return false;
    }
    this->run_claimed_ = true;
  }
  this->begin_scan_(run);
  return true;
}

void SecTouchSniffer::begin_scan_(const ScanRun &run) {
  if (run.goal == ScanGoal::SCAN_GOAL_CHECK) {
    ESP_LOGI(TAG, "Scan mode ON — check: every known property ID");
  } else if (run.listed) {
    ESP_LOGI(TAG, "Scan mode ON — discover: %d listed IDs, known IDs skipped", run.ids.count());
  } else {
    ESP_LOGI(TAG, "Scan mode ON — discover: IDs %d to %d, known IDs skipped", run.from, run.to);
  }
  {
    LockGuard guard(this->run_lock_);
    this->scan_run_ = run;
  }
  // No resume: every run begins at its goal's first ID.
  this->current_scan_id_ = first_scan_id(run);
  this->scan_completed_ = false;
  this->scan_ran_ = true;
  this->scan_retry_pending_ = false;
  this->pending_query_id_ = NO_PENDING_QUERY;
  this->running_ = Running::RUNNING_SCAN;
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
  this->running_ = Running::RUNNING_NONE;
  this->parent_->exit_scan_mode();
  this->release_run_();
}

int SecTouchSniffer::take_next_scan_id_() {
  const int id = this->current_scan_id_;
  this->current_scan_id_ = next_scan_id(this->scan_run_, id + 1);
  return id;
}

void SecTouchSniffer::query_(int property_id) {
  ESP_LOGD(TAG, "Scan: queuing GET for property_id %d", property_id);
  this->pending_query_id_ = property_id;
  this->parent_->add_discovery_get_task(property_id);
}

void SecTouchSniffer::on_queue_empty_() {
  if (this->running_ == Running::RUNNING_PROBE) {
    if (!this->probe_request_in_flight_) {
      return;
    }
    this->probe_request_in_flight_ = false;
    ScanOutcome outcome = this->parent_->get_last_scan_outcome();
    const bool was_get = this->probe_get_in_flight_;
    this->probe_get_in_flight_ = false;
    // An ok GET whose answer never reached the tap (another ID answered) has no value: a failed attempt.
    if (was_get && outcome == ScanOutcome::SCAN_OUTCOME_OK && !this->probe_read_seen_) {
      outcome = ScanOutcome::SCAN_OUTCOME_TIMEOUT;
    }
    ProbeCommand command{};
    {
      LockGuard guard(this->run_lock_);
      command = this->probe_.on_answer(outcome, this->probe_read_.view(), millis_64());
    }
    this->run_probe_command_(command);
    return;
  }
  if (this->is_scanning_) {
    this->on_scan_queue_empty_();
  }
}

void SecTouchSniffer::on_scan_queue_empty_() {
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

// --- Write probes ---

void SecTouchSniffer::begin_probe_(ProbeKind kind, uint8_t id, const ProbeValue &value) {
  const bool restore = kind == ProbeKind::PROBE_KIND_RESTORE;
  ESP_LOGI(TAG, "%s of property_id %d to '%.*s' — scan mode ON", restore ? "Restore" : "Write probe", id,
           static_cast<int>(value.len), value.bytes);
  this->running_ = Running::RUNNING_PROBE;
  this->probe_request_in_flight_ = false;
  this->probe_get_in_flight_ = false;
  // Pauses polling; the probe's own GETs and SETs are the only tasks until it ends.
  this->parent_->enter_scan_mode();
  ProbeCommand command{};
  {
    LockGuard guard(this->run_lock_);
    this->probe_ran_ = true;
    command = restore ? this->probe_.start_restore(id, value) : this->probe_.start_probe(id, value);
  }
  this->run_probe_command_(command);
}

void SecTouchSniffer::run_probe_command_(ProbeCommand command) {
  const int id = this->probe_.result().id;
  switch (command.action) {
    case ProbeAction::PROBE_ACTION_GET:
      this->probe_read_seen_ = false;
      this->probe_get_in_flight_ = true;
      this->probe_request_in_flight_ = true;
      this->parent_->add_discovery_get_task(id);
      return;
    case ProbeAction::PROBE_ACTION_SET: {
      // NUL-terminated for SetDataTask; set_value() is at most SET_VALUE_CAP bytes (probe_request_from()).
      char value[SET_VALUE_CAP + 1]{};
      const std::string_view set_value = this->probe_.set_value();
      std::memcpy(value, set_value.data(), std::min(set_value.size(), static_cast<size_t>(SET_VALUE_CAP)));
      if (this->parent_->add_discovery_set_task(id, value)) {
        this->probe_request_in_flight_ = true;
        return;
      }
      // Can't happen while the probe holds scan mode; counted as a SET that got no answer.
      ProbeCommand next{};
      {
        LockGuard guard(this->run_lock_);
        next = this->probe_.on_answer(ScanOutcome::SCAN_OUTCOME_TIMEOUT, "", millis_64());
      }
      this->run_probe_command_(next);
      return;
    }
    case ProbeAction::PROBE_ACTION_WAIT:
      this->set_timeout("probe_wait", command.wait_ms, [this]() {
        if (this->running_ != Running::RUNNING_PROBE) {
          return;
        }
        ProbeCommand next{};
        {
          LockGuard guard(this->run_lock_);
          next = this->probe_.on_wait_over();
        }
        this->run_probe_command_(next);
      });
      return;
    case ProbeAction::PROBE_ACTION_FINISHED:
      this->end_probe_();
      return;
  }
}

void SecTouchSniffer::end_probe_() {
  this->cancel_timeout("probe_wait");
  this->running_ = Running::RUNNING_NONE;
  this->probe_request_in_flight_ = false;
  this->probe_get_in_flight_ = false;
  const ProbeResult &result = this->probe_.result();
  if (result.restore == ProbeRestore::PROBE_RESTORE_FAILED) {
    ProbeValue now_at;
    const bool known = this->probe_.last_read(now_at);
    ESP_LOGE(TAG, "Restore failed: property_id %d is now '%.*s', was '%.*s'", result.id,
             known ? static_cast<int>(now_at.len) : 0, now_at.bytes, static_cast<int>(result.original.len),
             result.original.bytes);
  } else if (result.kind == ProbeKind::PROBE_KIND_RESTORE) {
    ESP_LOGI(TAG, "Restore of property_id %d: %s", result.id, probe_restore_name(result.restore));
  } else {
    ESP_LOGI(TAG, "Write probe of property_id %d: %s, restore %s", result.id, probe_verdict_name(result.verdict),
             probe_restore_name(result.restore));
  }
  this->parent_->exit_scan_mode();
  this->release_run_();
}

bool SecTouchSniffer::probe_result_(ProbeResult &out) {
  LockGuard guard(this->run_lock_);
  if (!this->probe_ran_) {
    return false;
  }
  out = this->probe_.result();
  return true;
}

// --- The message tap ---

void SecTouchSniffer::on_message_(int command_id, int property_id, const std::string &raw_value) {
  if (this->probe_get_in_flight_ && property_id == this->probe_.result().id) {
    this->probe_read_ = ProbeValue::of(raw_value);
    this->probe_read_seen_ = true;
  }

  const uint32_t now_s = uptime_s();
  RecordResult result;
  // Read under the lock too: the httpd task's clear also writes the store.
  uint32_t dropped = 0;
  uint32_t change_count = 0;
  {
    LockGuard guard(this->store_lock_);
    result = this->store_.record_message(command_id, property_id, raw_value, now_s);
    dropped = this->store_.dropped();
    if (result == RecordResult::RECORD_CHANGED) {
      change_count = this->store_.fields(property_id).change_count;
    }
  }

  if (result == RecordResult::RECORD_DROPPED) {
    if (dropped == 1) {
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
           raw_value.c_str(), now_s, change_count);
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

void SecTouchSniffer::handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
                                 size_t total) {
  if (index == 0) {
    this->body_received_ = 0;
  }
  // Only the first SET_VALUE_CAP + 1 bytes are kept: enough for probe_request_from() to refuse a longer value.
  for (size_t i = 0; i < len; i++) {
    if (this->body_received_ + i < sizeof(this->body_)) {
      this->body_[this->body_received_ + i] = static_cast<char>(data[i]);
    }
  }
  this->body_received_ += len;
}

void SecTouchSniffer::handleRequest(AsyncWebServerRequest *request) {
  switch (route_of(request)) {
    case SnifferRoute::ROUTE_PAGE:
      this->serve_page_(request);
      break;
    case SnifferRoute::ROUTE_DATA:
      this->serve_data_(*request);
      break;
    case SnifferRoute::ROUTE_SCAN_START:
      this->serve_scan_start_(*request);
      break;
    case SnifferRoute::ROUTE_SCAN_STOP:
      this->serve_scan_stop_(*request);
      break;
    case SnifferRoute::ROUTE_CLEAR:
      this->serve_clear_(*request);
      break;
    case SnifferRoute::ROUTE_PROBE:
      this->serve_probe_(*request, ProbeKind::PROBE_KIND_PROBE);
      break;
    case SnifferRoute::ROUTE_RESTORE:
      this->serve_probe_(*request, ProbeKind::PROBE_KIND_RESTORE);
      break;
    case SnifferRoute::ROUTE_NONE:
      break;
  }
  // The body belongs to this request only.
  this->body_received_ = 0;
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

// The 400 messages. Each %d is a cap, filled in from its constant.
static constexpr const char *BAD_RANGE_MESSAGE = "from and to must both be given, 0-%d, with from <= to";
static constexpr const char *BAD_IDS_MESSAGE = "ids must be a comma-separated list of 1-%d distinct IDs, 0-%d";
static constexpr const char *IDS_WITH_RANGE_MESSAGE = "give either ids or from and to, not both";
static constexpr const char *BAD_ID_MESSAGE = "id must be a property ID, 0-%d";
static constexpr const char *KNOWN_ID_MESSAGE = "id is a known property: its own entities write it";
static constexpr const char *BAD_VALUE_MESSAGE = "the value must be at most %d bytes, without NUL, TAB, STX or ETX";
static constexpr const char *UNREAD_BODY_MESSAGE =
    "send the value as the request body with a Content-Type such as text/plain";

void SecTouchSniffer::send_error_(httpd_req_t *req, const char *message) {
  this->begin_json_response_(req, HTTPD_400);
  this->json_.raw(R"({"error":)");
  this->json_.string(message);
  this->json_.raw("}");
  this->end_json_response_(req);
}

void SecTouchSniffer::send_busy_(httpd_req_t *req) {
  const ScanState scan = scan_state(this->scan_fields_());
  const bool has_probe = this->probe_result_(this->probe_scratch_);
  this->begin_json_response_(req, "409 Conflict");
  this->json_.raw(R"({"error":"busy","scan":)");
  write_scan_json(this->json_, scan);
  this->json_.raw(R"(,"probe":)");
  write_probe_json(this->json_, has_probe ? &this->probe_scratch_ : nullptr);
  this->json_.raw("}");
  this->end_json_response_(req);
}

void SecTouchSniffer::serve_data_(httpd_req_t *req) {
  const StringRef name = App.get_name();
  const DeviceInfo device{
      .mac = this->mac_,
      .name = std::string_view(name.c_str(), name.size()),
      .boot = this->boot_,
      .uptime_ms = millis_64(),
  };
  // Read before the records, so a page that sees `done` also has the last answer.
  const ScanState scan = scan_state(this->scan_fields_());
  const bool has_probe = this->probe_result_(this->probe_scratch_);
  this->begin_json_response_(req);
  write_data_json(
      this->json_, this->store_, device, scan, has_probe ? &this->probe_scratch_ : nullptr, since_from_uri(req->uri),
      this->record_scratch_,
      [this](auto read) {
        LockGuard guard(this->store_lock_);
        read();
      },
      this->is_registered_());
  this->end_json_response_(req);
}

void SecTouchSniffer::serve_scan_start_(httpd_req_t *req) {
  const ScanStart parsed = scan_start_from_uri(req->uri);
  switch (parsed.error) {
    case ScanStartError::SCAN_START_OK:
      break;
    case ScanStartError::SCAN_START_BAD_RANGE:
      snprintf(this->message_, sizeof(this->message_), BAD_RANGE_MESSAGE, MAX_PROPERTY_ID);
      this->send_error_(req, this->message_);
      return;
    case ScanStartError::SCAN_START_BAD_IDS:
      snprintf(this->message_, sizeof(this->message_), BAD_IDS_MESSAGE, MAX_SCAN_LIST_IDS, MAX_PROPERTY_ID);
      this->send_error_(req, this->message_);
      return;
    case ScanStartError::SCAN_START_IDS_WITH_RANGE:
      this->send_error_(req, IDS_WITH_RANGE_MESSAGE);
      return;
  }
  if (!this->request_scan_start(parsed.run)) {
    this->send_busy_(req);
    return;
  }
  this->begin_json_response_(req);
  write_scan_json(this->json_, scan_start_reply(parsed.run));
  this->end_json_response_(req);
}

void SecTouchSniffer::serve_scan_stop_(httpd_req_t *req) {
  const ScanFields fields = this->scan_fields_();
  this->request_scan_stop();
  this->begin_json_response_(req);
  write_scan_json(this->json_, scan_stop_reply(fields));
  this->end_json_response_(req);
}

void SecTouchSniffer::serve_probe_(httpd_req_t *req, ProbeKind kind) {
  // A form's body (or one without a Content-Type) never reaches handleBody(): the server reads it for itself.
  if (req->content_len > 0 && this->body_received_ != req->content_len) {
    this->send_error_(req, UNREAD_BODY_MESSAGE);
    return;
  }
  const size_t kept = std::min(this->body_received_, sizeof(this->body_));
  const ProbeRequest parsed = probe_request_from(req->uri, std::string_view(this->body_, kept));
  switch (parsed.error) {
    case ProbeRequestError::PROBE_REQUEST_OK:
      break;
    case ProbeRequestError::PROBE_REQUEST_BAD_ID:
      snprintf(this->message_, sizeof(this->message_), BAD_ID_MESSAGE, MAX_PROPERTY_ID);
      this->send_error_(req, this->message_);
      return;
    case ProbeRequestError::PROBE_REQUEST_KNOWN_ID:
      this->send_error_(req, KNOWN_ID_MESSAGE);
      return;
    case ProbeRequestError::PROBE_REQUEST_BAD_VALUE:
      snprintf(this->message_, sizeof(this->message_), BAD_VALUE_MESSAGE, SET_VALUE_CAP);
      this->send_error_(req, this->message_);
      return;
  }
  if (!this->request_probe(kind, parsed.id, parsed.value)) {
    this->send_busy_(req);
    return;
  }
  this->probe_scratch_ = probe_start_result(kind, parsed.id, parsed.value);
  this->begin_json_response_(req);
  write_probe_json(this->json_, &this->probe_scratch_);
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
