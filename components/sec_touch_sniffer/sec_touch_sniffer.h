#pragma once

#include <string>
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/switch/switch.h"
#include "../sec_touch/sec_touch.h"
#include "property_store.h"

namespace esphome {
namespace sec_touch {

class SecTouchSniffer : public Component, public text_sensor::TextSensor {
  static const char *const TAG;

 public:
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

  void setup() override;
  void dump_config() override;

 protected:
  SECTouchComponent *parent_;
  switch_::Switch *scan_switch_{nullptr};

  PropertyStore store_;
  /// The text sensor state, reserved once in setup() and rebuilt in place.
  std::string state_;
  bool web_page_{false};

  int scan_start_{1};
  int scan_end_{0};
  int current_scan_id_{0};
  /// pending_query_id_ when no scan query is in flight.
  static constexpr int NO_PENDING_QUERY = -1;
  /// The ID of the scan query whose outcome on_queue_empty_() records next.
  int pending_query_id_{NO_PENDING_QUERY};
  bool is_scanning_{false};
  bool scan_retry_pending_{false};

  void update_scan_switch_();
  void on_message_(int command_id, int property_id, const std::string &raw_value);
  void on_queue_empty_();
  void query_(int property_id);
  void publish_records_();
  /// Whether an entity registered a property ID, as the state builder takes it.
  auto is_registered_() const {
    return [this](int id) { return this->parent_->is_property_registered(id); };
  }
};

}  // namespace sec_touch
}  // namespace esphome
