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
#include "sniffer_data.h"

#ifdef USE_SEC_TOUCH_SNIFFER_WEB_PAGE
#include "esphome/components/web_server_base/web_server_base.h"
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

  void set_scan_range(int start, int end) {
    this->scan_start_ = start;
    this->scan_end_ = end;
  }
  void set_scan_switch(switch_::Switch *s) { this->scan_switch_ = s; }
  /// With the page on, the store keeps a history of previous raw values.
  void set_web_page(bool web_page) { this->web_page_ = web_page; }

  bool is_scanning() const { return this->is_scanning_; }
  void toggle_scan();
  /// Queues a scan start or stop from any task (the page's handler runs on the httpd task); the next loop() applies
  /// it. Idempotent: a start while scanning or a stop while idle changes nothing. Ignored without a scan range.
  void request_scan(bool start);

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

  int scan_start_{1};
  int scan_end_{0};
  // Atomic: the httpd task reads them for the page's scan object.
  std::atomic<int> current_scan_id_{0};
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
  void publish_records_();
  ScanState scan_state_() const {
    return scan_state(this->scan_start_, this->scan_end_, this->current_scan_id_, this->is_scanning_);
  }
  /// Whether an entity registered a property ID, as the state builder takes it.
  auto is_registered_() const {
    return [this](int id) { return this->parent_->is_property_registered(id); };
  }

  /// A request_scan() not applied yet.
  enum class ScanRequest : uint8_t { SCAN_REQUEST_NONE, SCAN_REQUEST_START, SCAN_REQUEST_STOP };
  std::atomic<ScanRequest> scan_request_{ScanRequest::SCAN_REQUEST_NONE};

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
  void begin_json_response_(httpd_req_t *req);
  void end_json_response_(httpd_req_t *req);
  void serve_data_(httpd_req_t *req);
  void serve_scan_(httpd_req_t *req, bool start);
#endif
};

}  // namespace sec_touch
}  // namespace esphome
