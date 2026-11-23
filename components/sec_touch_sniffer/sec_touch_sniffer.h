#pragma once

#include <atomic>
#include <string>
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "../sec_touch/sec_touch.h"
#include "property_store.h"
#include "scan_goal.h"
#include "sniffer_data.h"
#include "write_probe.h"

// The config allows ESP32 only; the host test build has no httpd, so it builds without the page handler.
#ifdef USE_ESP32
#include "esphome/components/web_server_base/web_server_base.h"

/// The sniffer page, gzipped, in flash. The codegen (__init__.py, PAGE_ARRAY) bundles web/ and emits both.
extern const uint8_t SEC_TOUCH_SNIFFER_PAGE[] PROGMEM;
extern const size_t SEC_TOUCH_SNIFFER_PAGE_SIZE;
#endif

namespace esphome {
namespace sec_touch {

class SecTouchSniffer : public Component
#ifdef USE_ESP32
    ,
                        public AsyncWebHandler
#endif
{
  static const char *const TAG;

 public:
  /// Gap between one scan query's outcome and the next query.
  static constexpr uint32_t SCAN_NEXT_DELAY_MS = 160;

  explicit SecTouchSniffer(SECTouchComponent *parent);

  bool is_scanning() const { return this->is_scanning_; }
  /// Whether a write probe or restore runs.
  bool is_probing() const { return this->running_ == Running::RUNNING_PROBE; }

  // One run at a time: a scan, a probe and a restore share one exclusive slot. Each start claims it atomically and
  // the run's end frees it, so a start while another run holds it is refused and changes nothing.

  /// Claims the slot and starts a scan of `run` at its first ID; on the main loop only. False when the slot is busy.
  bool start_scan(const ScanRun &run);
  /// Stops the scan; on the main loop only. A stop while idle, or during a probe or restore, changes nothing.
  void stop_scan();
  /// From any task (the page's handler runs on the httpd task): claims the slot for a scan of `run` and queues its
  /// start for the next loop(). False when the slot is busy: nothing is queued.
  bool request_scan_start(const ScanRun &run);
  /// From any task: claims the slot for a write probe of `value` on `id` (PROBE_KIND_PROBE), or a restore of `id` to
  /// the original `value` (PROBE_KIND_RESTORE), and queues it for the next loop(). False when the slot is busy.
  bool request_probe(ProbeKind kind, uint8_t id, const ProbeValue &value);
  /// From any task: queues a stop of the running or queued scan for the next loop(). Nothing is queued while no scan
  /// runs or waits, so a stop during a probe or restore, or while idle, changes nothing.
  void request_scan_stop();

  void setup() override;
  void dump_config() override;
  /// Applies a queued start, then a queued stop. Disabled until one is queued.
  void loop() override;

#ifdef USE_ESP32
  /// After WiFi, like every component that serves HTTP: init() starts the server.
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }

  // Run on the httpd task, not the main loop.
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  /// A POST body sent with a Content-Type other than a form's: the probe's value.
  void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override;
#endif

 protected:
  SECTouchComponent *parent_;

  PropertyStore store_;
  /// Guards store_: the main loop writes it while the httpd task serves it.
  Mutex store_lock_;

  /// What the main loop runs. Read and written on the main loop only.
  enum class Running : uint8_t { RUNNING_NONE, RUNNING_SCAN, RUNNING_PROBE };
  Running running_{Running::RUNNING_NONE};

  /// A start claimed by a request_*() and not applied by loop() yet.
  struct PendingRun {
    enum class Kind : uint8_t { PENDING_NONE, PENDING_SCAN, PENDING_PROBE };
    Kind kind;
    ScanRun scan;
    ProbeKind probe_kind;
    uint8_t probe_id;
    ProbeValue probe_value;
  };

  /// Guards the run slot: run_claimed_, pending_, stop_pending_, scan_run_ and probe_.
  Mutex run_lock_;
  /// A run holds the slot: queued by a request_*() or running, until its end frees it.
  bool run_claimed_{false};
  PendingRun pending_{};
  /// A stop of the running or queued scan. A run's end drops it, so it never stops the run after.
  bool stop_pending_{false};
  /// The running or last scan run. The main loop writes it under run_lock_ at each start and reads it without the
  /// lock, as its only writer.
  ScanRun scan_run_{DEFAULT_DISCOVER_RUN};
  /// The running or last probe or restore; probe_ran_ false before any since boot. Every call into it takes
  /// run_lock_, since the httpd task copies its result.
  WriteProbe probe_;
  bool probe_ran_{false};

  // Atomic: the httpd task reads them for the page's scan object (ScanFields).
  std::atomic<bool> scan_ran_{false};
  std::atomic<bool> scan_completed_{false};
  std::atomic<int> current_scan_id_{NO_SCAN_ID};
  /// pending_query_id_ when no scan query is in flight.
  static constexpr int NO_PENDING_QUERY = -1;
  /// The ID of the scan query whose outcome on_queue_empty_() records next.
  int pending_query_id_{NO_PENDING_QUERY};
  std::atomic<bool> is_scanning_{false};
  bool scan_retry_pending_{false};

  /// A probe GET or SET is in flight: on_queue_empty_() hands its outcome to probe_.
  bool probe_request_in_flight_{false};
  /// A probe GET is in flight and the tap saw its answer: probe_read_ holds it.
  bool probe_get_in_flight_{false};
  bool probe_read_seen_{false};
  ProbeValue probe_read_{};

  void on_message_(int command_id, int property_id, const std::string &raw_value);
  void on_queue_empty_();
  void on_scan_queue_empty_();
  void query_(int property_id);
  /// The ID to query next; moves current_scan_id_ past it.
  int take_next_scan_id_();
  /// Claims the slot for `run` and queues it. False when the slot is busy.
  bool request_run_(const PendingRun &run);
  /// Frees the slot at a run's end, and drops a stop queued for it.
  void release_run_();
  /// Starts a scan the slot is already claimed for.
  void begin_scan_(const ScanRun &run);
  /// Ends the running scan: `completed` after it covered all its IDs, otherwise stopped.
  void end_scan_(bool completed);
  /// Starts a probe or restore the slot is already claimed for.
  void begin_probe_(ProbeKind kind, uint8_t id, const ProbeValue &value);
  /// Does what probe_ asked for next.
  void run_probe_command_(ProbeCommand command);
  void end_probe_();
  /// Copies the running or last probe's result into `out`; false (and nothing copied) before any.
  bool probe_result_(ProbeResult &out);
  ScanFields scan_fields_();
  /// Whether an entity registered a property ID, as the page's records take it. Safe from the httpd task: entities
  /// register in their constructors or in setup() at a higher priority than the sniffer's, which starts the server.
  auto is_registered_() const {
    return [this](int id) { return this->parent_->is_property_registered(id); };
  }

#ifdef USE_ESP32
  /// Hex MAC, which the page files its browser storage under.
  char mac_[MAC_ADDRESS_BUFFER_SIZE]{};
  /// A random id per boot, so the page notices a reboot; a clear draws a new one, as a reboot would.
  char boot_[format_hex_size(sizeof(uint32_t))]{};

  // Serving scratch, static so a request grows no heap and keeps the httpd task's stack small. One request runs at
  // a time: the httpd server has a single task.
  RecordSnapshot record_scratch_;
  ProbeResult probe_scratch_;
  JsonChunkWriter json_{&SecTouchSniffer::send_chunk_, nullptr};
  /// The POST body handleBody() received: its first SET_VALUE_CAP + 1 bytes, enough to tell a value over the cap.
  char body_[SET_VALUE_CAP + 1]{};
  size_t body_received_{0};
  /// A 400 message with its caps filled in.
  char message_[96]{};

  static bool send_chunk_(void *request, const char *data, size_t len);
  void begin_json_response_(httpd_req_t *req, const char *status = HTTPD_200);
  void end_json_response_(httpd_req_t *req);
  /// A 400 with `{"error": message}`.
  void send_error_(httpd_req_t *req, const char *message);
  /// The 409 of a busy start: the current scan and probe objects.
  void send_busy_(httpd_req_t *req);
  void serve_page_(AsyncWebServerRequest *request);
  void serve_data_(httpd_req_t *req);
  void serve_scan_start_(httpd_req_t *req);
  void serve_scan_stop_(httpd_req_t *req);
  void serve_probe_(httpd_req_t *req, ProbeKind kind);
  /// Empties the record store and draws a new boot id, so every open page resyncs to the empty store.
  void serve_clear_(httpd_req_t *req);
  void new_boot_id_();
#endif
};

}  // namespace sec_touch
}  // namespace esphome
