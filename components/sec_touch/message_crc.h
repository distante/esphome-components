#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_message_crc.

#include <cstdint>

#include "XModemCRC.h"

namespace esphome {
namespace sec_touch {

/// Result of checking the CRC field of an incoming data message.
enum class CrcCheck : uint8_t {
  /// The CRC field matches the XModem CRC of the message.
  CRC_MATCH,
  /// The CRC field is a number, but not the CRC of the message.
  CRC_MISMATCH,
  /// The CRC field is empty, not decimal digits, or above 65535.
  CRC_NOT_A_NUMBER,
  /// The message has no CRC field (`crc_tab` is -1): nothing to check.
  CRC_NO_FIELD,
};

/// Checks the CRC field of a data message `[STX]cmd[TAB]prop[TAB]value[TAB]crc[ETX]`.
///
/// `buf` holds the whole message, from STX at `buf[0]` to ETX at `buf[len - 1]`. `crc_tab` is the index of the TAB
/// before the CRC field. The CRC covers STX up to and including that TAB, the same rule as outgoing messages.
inline CrcCheck check_message_crc(const char *buf, int len, int crc_tab) {
  if (crc_tab < 0) {
    return CrcCheck::CRC_NO_FIELD;
  }

  // The field runs from after the TAB up to, not including, the ETX.
  const int field_start = crc_tab + 1;
  const int field_end = len - 1;
  if (field_end <= field_start) {
    return CrcCheck::CRC_NOT_A_NUMBER;
  }

  uint32_t received = 0;
  for (int i = field_start; i < field_end; ++i) {
    if (buf[i] < '0' || buf[i] > '9') {
      return CrcCheck::CRC_NOT_A_NUMBER;
    }
    received = received * 10 + static_cast<uint32_t>(buf[i] - '0');
    if (received > 0xFFFF) {
      return CrcCheck::CRC_NOT_A_NUMBER;
    }
  }

  const unsigned short computed = GetXModemCRC(buf, static_cast<unsigned>(crc_tab + 1));
  if (received != computed) {
    return CrcCheck::CRC_MISMATCH;
  }
  return CrcCheck::CRC_MATCH;
}

}  // namespace sec_touch
}  // namespace esphome
