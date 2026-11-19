#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_data_message.

#include <cstdint>

#include "message_crc.h"

namespace esphome {
namespace sec_touch {

/// Shortest frame that can be a data message: `[STX]` + at least five bytes + `[ETX]`. Anything shorter is an ACK,
/// a NAK, or corruption.
static constexpr int DATA_MESSAGE_MIN_LEN = 7;

/// Positions of the first three TABs in a data message `[STX]cmd[TAB]prop[TAB]value[TAB]crc[ETX]`, -1 when absent.
/// TABs are found left to right, so `tab2 == -1` implies `tab3 == -1`.
struct DataMessageTabs {
  int tab1 = -1;
  int tab2 = -1;
  int tab3 = -1;
};

/// What the hub does with a data message.
enum class DataMessageCheck : uint8_t {
  /// The CRC field matches: route the message.
  DATA_ROUTE,
  /// No CRC field and `optional_crc: true`: route the message unchecked.
  DATA_ROUTE_UNCHECKED,
  /// Not enough TABs: no property ID, or no CRC field with `optional_crc: false`.
  DATA_MISSING_FIELDS,
  /// The CRC field is a number, but not the CRC of the message.
  DATA_CRC_MISMATCH,
  /// The CRC field is empty, not decimal digits, or above 65535.
  DATA_CRC_NOT_A_NUMBER,
};

/// Finds the first three TABs in `buf[0..len)`.
inline DataMessageTabs find_data_message_tabs(const char *buf, int len) {
  DataMessageTabs tabs;
  int found = 0;
  for (int i = 0; i < len && found < 3; ++i) {
    if (buf[i] != '\t') {
      continue;
    }
    if (found == 0) {
      tabs.tab1 = i;
    } else if (found == 1) {
      tabs.tab2 = i;
    } else {
      tabs.tab3 = i;
    }
    ++found;
  }
  return tabs;
}

/// Decides whether a data message is routed. `buf` holds the whole message, STX at `buf[0]` to ETX at
/// `buf[len - 1]`. `optional_crc` only matters when the CRC field is missing: a CRC that is there is always checked.
inline DataMessageCheck check_data_message(const char *buf, int len, const DataMessageTabs &tabs, bool optional_crc) {
  if (tabs.tab1 == -1 || tabs.tab2 == -1) {
    return DataMessageCheck::DATA_MISSING_FIELDS;
  }
  if (tabs.tab3 == -1) {
    return optional_crc ? DataMessageCheck::DATA_ROUTE_UNCHECKED : DataMessageCheck::DATA_MISSING_FIELDS;
  }

  const CrcCheck crc = check_message_crc(buf, len, tabs.tab3);
  if (crc == CrcCheck::CRC_MISMATCH) {
    return DataMessageCheck::DATA_CRC_MISMATCH;
  }
  if (crc == CrcCheck::CRC_NOT_A_NUMBER) {
    return DataMessageCheck::DATA_CRC_NOT_A_NUMBER;
  }
  return DataMessageCheck::DATA_ROUTE;
}

/// Why a data message was rejected, for logs. Empty for the two checks that route the message.
inline const char *data_message_check_reason(DataMessageCheck check) {
  switch (check) {
    case DataMessageCheck::DATA_ROUTE:
    case DataMessageCheck::DATA_ROUTE_UNCHECKED:
      return "";
    case DataMessageCheck::DATA_MISSING_FIELDS:
      return "Not enough TABs in message";
    case DataMessageCheck::DATA_CRC_MISMATCH:
      return "CRC mismatch";
    case DataMessageCheck::DATA_CRC_NOT_A_NUMBER:
      return "CRC field is not a number";
  }
  return "";
}

}  // namespace sec_touch
}  // namespace esphome
