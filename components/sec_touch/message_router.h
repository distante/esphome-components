#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_message_router.

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace esphome {
namespace sec_touch {

/**
 * Observes every data message the hub receives, with its raw value byte for byte.
 * Fires in every mode, before the hub routes the message to entity listeners.
 */
using MessageTap = std::function<void(int command_id, int property_id, const std::string &raw_value)>;

/// What the hub does with a data message after the taps have seen it.
enum class Route {
  /// Hand the message to the entity listeners registered for its property ID.
  DELIVER,
  /// Nobody else wants it: scan mode is on, or no entity listener matched but a tap saw it.
  DROP,
  /// No entity listener matched and no tap is registered: the message reached no one.
  WARN_NO_LISTENER,
};

class MessageRouter {
 public:
  void add_tap(MessageTap tap) { this->taps_.push_back(std::move(tap)); }

  /**
   * Fires every tap, then says where the message goes next.
   * @param has_entity_listener whether an entity listener is registered for property_id
   */
  Route dispatch(int command_id, int property_id, const std::string &raw_value, bool scan_mode,
                 bool has_entity_listener) const {
    for (const auto &tap : this->taps_) {
      tap(command_id, property_id, raw_value);
    }
    if (scan_mode) {
      return Route::DROP;
    }
    if (has_entity_listener) {
      return Route::DELIVER;
    }
    if (this->taps_.empty()) {
      return Route::WARN_NO_LISTENER;
    }
    return Route::DROP;
  }

 protected:
  std::vector<MessageTap> taps_;
};

}  // namespace sec_touch
}  // namespace esphome
