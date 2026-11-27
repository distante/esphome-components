#include <gtest/gtest.h>

#include <cstdint>

#include "sec_touch/time_program_codec.h"
#include "sec_touch/time_program_mask.h"

using esphome::sec_touch::MaskMerge;
using esphome::sec_touch::known_property_detail::MAX_ENABLED_MASK;
using esphome::sec_touch::switch_time_bit;

namespace {

constexpr bool ENABLE = true;
constexpr bool DISABLE = false;
constexpr uint8_t NONE = 0;
/// Every switch time's bit.
constexpr uint8_t ALL = static_cast<uint8_t>(MAX_ENABLED_MASK);

constexpr uint8_t bit(int switch_time) { return switch_time_bit(switch_time); }

}  // namespace

TEST(MaskMerge, StartsWithNothingPending) {
  MaskMerge merge;
  EXPECT_FALSE(merge.has_pending());
  EXPECT_EQ(merge.apply(bit(2)), bit(2));
}

// AC 7: only the requested bits change.
TEST(MaskMerge, ChangesOnlyTheRequestedBits) {
  MaskMerge merge;
  merge.request(4, ENABLE);
  merge.request(1, DISABLE);

  EXPECT_TRUE(merge.has_pending());
  EXPECT_EQ(merge.apply(bit(1) | bit(2)), bit(2) | bit(4));
  EXPECT_EQ(merge.apply(NONE), bit(4));
  EXPECT_EQ(merge.apply(ALL), ALL & ~bit(1));
}

// Writing: "Changes merge; per bit, the later request wins."
TEST(MaskMerge, TheLaterRequestForABitWins) {
  MaskMerge merge;
  merge.request(3, ENABLE);
  merge.request(3, DISABLE);
  EXPECT_EQ(merge.apply(bit(3)), NONE);

  merge.request(3, ENABLE);
  EXPECT_EQ(merge.apply(NONE), bit(3));
}

TEST(MaskMerge, ClearDropsEveryRequest) {
  MaskMerge merge;
  merge.request(5, ENABLE);
  merge.clear();

  EXPECT_FALSE(merge.has_pending());
  EXPECT_EQ(merge.apply(NONE), NONE);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
