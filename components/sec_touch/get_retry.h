#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_get_retry.

#include "data_message.h"

namespace esphome {
namespace sec_touch {

/// Whether the hub sends a GET once more after dropping its reply (issue #52). `dropped_reply` is the check that
/// dropped a data frame which arrived while the GET was in flight. A timeout or a frame too short for data never
/// gets here. Only the checks listed below earn a retry, so a new check doesn't by default. One retry per task, none
/// in scan mode: the sniffer retries scan queries itself.
inline bool should_retry_get(bool is_get, DataMessageCheck dropped_reply, bool already_retried, bool scan_mode) {
  if (!is_get || already_retried || scan_mode) {
    return false;
  }
  return dropped_reply == DataMessageCheck::DATA_MISSING_FIELDS ||
         dropped_reply == DataMessageCheck::DATA_CRC_MISMATCH ||
         dropped_reply == DataMessageCheck::DATA_CRC_NOT_A_NUMBER || dropped_reply == DataMessageCheck::DATA_STX_LOST;
}

}  // namespace sec_touch
}  // namespace esphome
