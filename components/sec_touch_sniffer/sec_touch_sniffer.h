#pragma once

#include <atomic>
#include <string>
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/switch/switch.h"
#include "../sec_touch/sec_touch.h"
#include "property_store.h"
#include "scan_goal.h"
#include "sniffer_data.h"

#ifdef USE_SEC_TOUCH_SNIFFER_WEB_PAGE
#include "esphome/components/web_server_base/web_server_base.h"

/// The sniffer page, gzipped, in flash. The codegen (text_sensor.py, PAGE_ARRAY) bundles web/ and emits both.
extern const uint8_t SEC_TOUCH_SNIFFER_PAGE[] PROGMEM;
extern const size_t SEC_TOUCH_SNIFFER_PAGE_SIZE;
#endif

namespace esphome {
namespace sec_touch {

class SecTouchSniffer : public Component,
                        public text_sensor::TextSensor
#ifdef USE_SEC_TOUCH_SNIFFER_WEB_PAGE
    ,
                        public AsyncWebHandler
#endif
{
  static const char *const TAG;

 public:
  /// Wait after a scan query's first timeout before its one retry.
  static constexpr uint32_t SCAN_RETRY_DELAY_MS = 5000;
  /// Gap between one scan query's outcome and the next query.
  static constexpr uint32_t SCAN_NEXT_DELAY_MS = 160;

  explicit SecTouchSniffer(SECTouchComponent *parent);

  void set_scan_switch(switch_::Switch *s) { this->scan_switch_ = s; }
  /// With the page on, the store keeps a history of previous raw values.
  void set_web_page(bool web_page) { this->web_page_ = web_page; }

  bool is_scanning() const { return this->is_scanning_; }
  /// Starts a scan of `run` at its first ID; on the main loop only. A start while scanning changes nothing, whatever
  /// its goal or range.
  void start_scan(const ScanRun &run);
  /// Stops the scan; on the main loop only. A stop while idle changes nothing.
  void stop_scan();
  /// Queues a scan start of `run`, or a stop, from any task (the page's handler runs on the httpd task); the next
  /// loop() applies it with start_scan() or stop_scan().
  void request_scan(bool start, const ScanRun &run = DEFAULT_DISCOVER_RUN);

  void setup() override;
  void dump_config() override;
  /// Applies a queued request_scan(). Disabled until one is queued.
  void loop() override;

#ifdef USE_SEC_TOUCH_SNIFFER_WEB_PAGE
  /// After WiFi, like every component that serves HTTP: init() starts the server.
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }

  // Run on the httpd task, not the main loop.
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
#endif

 protected:
  SECTouchComponent *parent_;
  switch_::Switch *scan_switch_{nullptr};

  PropertyStore store_;
  /// Guards store_: the main loop writes it while the httpd task serves it.
  Mutex store_lock_;
  /// The text sensor state, reserved once in setup() and rebuilt in place.
  std::string state_;
  bool web_page_{false};

  // Atomic: the httpd task reads them for the page's scan object (ScanFields).
  std::atomic<bool> scan_ran_{false};
  std::atomic<bool> scan_completed_{false};
  std::atomic<ScanGoal> scan_goal_{DEFAULT_DISCOVER_RUN.goal};
  std::atomic<uint8_t> scan_from_{DEFAULT_DISCOVER_RUN.from};
  std::atomic<uint8_t> scan_to_{DEFAULT_DISCOVER_RUN.to};
  std::atomic<int> current_scan_id_{NO_SCAN_ID};
  /// pending_query_id_ when no scan query is in flight.
  static constexpr int NO_PENDING_QUERY = -1;
  /// The ID of the scan query whose outcome on_queue_empty_() records next.
  int pending_query_id_{NO_PENDING_QUERY};
  std::atomic<bool> is_scanning_{false};
  bool scan_retry_pending_{false};

  void update_scan_switch_();
  void on_message_(int command_id, int property_id, const std::string &raw_value);
  void on_queue_empty_();
  void query_(int property_id);
  /// The ID to query next; moves current_scan_id_ past it.
  int take_next_scan_id_();
  /// Ends the running scan: `completed` after it covered all its IDs, otherwise stopped.
  void end_scan_(bool completed);
  void publish_records_();
  ScanRun scan_run_() const { return ScanRun{this->scan_goal_, this->scan_from_, this->scan_to_}; }
  ScanFields scan_fields_() const {
    return ScanFields{.ran = this->scan_ran_,
                      .active = this->is_scanning_,
                      .completed = this->scan_completed_,
                      .run = this->scan_run_(),
                      .current = this->current_scan_id_};
  }
  /// Whether an entity registered a property ID, as the state builder takes it. Safe from the httpd task: entities
  /// register in their constructors or in setup() at a higher priority than the sniffer's, which starts the server.
  auto is_registered_() const {
    return [this](int id) { return this->parent_->is_property_registered(id); };
  }

  /// A request_scan() not applied yet.
  enum class ScanRequest : uint8_t { SCAN_REQUEST_NONE, SCAN_REQUEST_START, SCAN_REQUEST_STOP };
  /// A request and, for a start, its run. Four bytes in one atomic, so loop() never takes one request's kind with
  /// another's run.
  struct PendingScanRequest {
    ScanRequest kind;
    ScanRun run;
  };
  static constexpr PendingScanRequest NO_SCAN_REQUEST{ScanRequest::SCAN_REQUEST_NONE, DEFAULT_DISCOVER_RUN};
  std::atomic<PendingScanRequest> scan_request_{NO_SCAN_REQUEST};

#ifdef USE_SEC_TOUCH_SNIFFER_WEB_PAGE
  /// Hex MAC, which the page files its browser storage under.
  char mac_[MAC_ADDRESS_BUFFER_SIZE]{};
  /// A random id per boot, so the page notices a reboot.
  char boot_[format_hex_size(sizeof(uint32_t))]{};

  // Serving scratch, static so a request grows no heap and keeps the httpd task's stack small. One request runs at
  // a time: the httpd server has a single task.
  RecordSnapshot record_scratch_;
  JsonChunkWriter json_{&SecTouchSniffer::send_chunk_, nullptr};

  static bool send_chunk_(void *request, const char *data, size_t len);
  void begin_json_response_(httpd_req_t *req, const char *status = HTTPD_200);
  void end_json_response_(httpd_req_t *req);
  void serve_page_(AsyncWebServerRequest *request);
  void serve_data_(httpd_req_t *req);
  void serve_scan_(httpd_req_t *req, bool start);
#endif
};

}  // namespace sec_touch
}  // namespace esphome
