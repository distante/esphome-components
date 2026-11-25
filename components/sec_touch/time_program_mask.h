#pragma once

// Plain C++, no ESPHome includes: the bit changes requested for a pair's enabled mask, applied to a fresh mask when
// the write is due (.specs/time-programs.md, "Writing"). Built on the host by tests/cpp/test_time_program_mask.

#include <cstdint>

#include "time_program_codec.h"

namespace esphome {
namespace sec_touch {

class MaskMerge {
 public:
  /// Asks for switch time `switch_time`'s bit to be set (`enabled`) or cleared. Per bit, the later request wins.
  void request(int switch_time, bool enabled) {
    const uint8_t bit = switch_time_bit(switch_time);
    if (enabled) {
      this->set_bits_ |= bit;
      this->clear_bits_ &= static_cast<uint8_t>(~bit);
      return;
    }
    this->clear_bits_ |= bit;
    this->set_bits_ &= static_cast<uint8_t>(~bit);
  }

  bool has_pending() const { return (this->set_bits_ | this->clear_bits_) != 0; }

  /// `fresh` with only the requested bits changed; every other bit stays as it is.
  uint8_t apply(uint8_t fresh) const {
    return static_cast<uint8_t>((fresh | this->set_bits_) & static_cast<uint8_t>(~this->clear_bits_));
  }

  void clear() {
    this->set_bits_ = 0;
    this->clear_bits_ = 0;
  }

 protected:
  uint8_t set_bits_{0};
  uint8_t clear_bits_{0};
};

}  // namespace sec_touch
}  // namespace esphome
