#pragma once

#include <map>
#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/time/real_time_clock.h"
#include "../sec_touch/sec_touch.h"

namespace esphome {
namespace sec_touch {

struct SniffedEntry {
  int command_id;
  int value;
  char last_seen_at[20];  // "dd-mm-yyyy HH:MM" fits in 16 chars; fixed size avoids heap churn
};

class SecTouchSniffer : public Component, public text_sensor::TextSensor {
  static const char *const TAG;
  // Text sensor payload cap: keeps output within comfortable MQTT/API packet sizes.
  // Each entry is ~35 chars; 2048 chars fits ~58 entries before truncation.
  static constexpr size_t MAX_STATE_LEN = 2048;

 public:
  explicit SecTouchSniffer(SECTouchComponent *parent);

  void set_time(time::RealTimeClock *time) { this->time_ = time; }
  void set_scan_range(int start, int end) {
    this->scan_start_ = start;
    this->scan_end_ = end;
  }
  void set_scan_switch(switch_::Switch *s) { this->scan_switch_ = s; }

  bool is_scanning() const { return this->is_scanning_; }
  void toggle_scan();

  void setup() override;
  void dump_config() override;

 protected:
  SECTouchComponent *parent_;
  time::RealTimeClock *time_{nullptr};
  switch_::Switch *scan_switch_{nullptr};

  // Active scan not recommended on ESP8266 — each entry takes ~80-100 bytes;
  // a 200-ID scan uses ~20 KB against only ~40 KB usable heap.
  std::map<int, SniffedEntry> discovered_ids_;

  int scan_start_{1};
  int scan_end_{0};
  int current_scan_id_{0};
  bool is_scanning_{false};
  bool scan_retry_pending_{false};

  void update_scan_switch_();
  void on_raw_message_(int command_id, int property_id, int new_value);
  void on_queue_empty_();
  std::string get_timestamp_() const;
  std::string build_state_string_() const;
};

}  // namespace sec_touch
}  // namespace esphome
