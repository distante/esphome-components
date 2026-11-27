#include <gtest/gtest.h>

#include <cstdint>

#include "sec_touch/time_units.h"

using esphome::sec_touch::millis_reached;

namespace {

constexpr uint32_t AT_MS = 1000;
/// A moment 10 ms before millis() wraps to 0.
constexpr uint32_t BEFORE_WRAP_MS = UINT32_MAX - 9;
/// How far past BEFORE_WRAP_MS the tests look: across the wrap.
constexpr uint32_t ACROSS_WRAP_MS = 20;

}  // namespace

TEST(MillisReached, ReachedAtAndAfterTheMoment) {
  EXPECT_TRUE(millis_reached(AT_MS, AT_MS));
  EXPECT_TRUE(millis_reached(AT_MS + 1, AT_MS));
  EXPECT_FALSE(millis_reached(AT_MS - 1, AT_MS));
}

TEST(MillisReached, AMomentBeforeTheWrapIsReachedAfterIt) {
  EXPECT_TRUE(millis_reached(BEFORE_WRAP_MS + ACROSS_WRAP_MS, BEFORE_WRAP_MS));
}

TEST(MillisReached, AMomentAfterTheWrapIsNotReachedBeforeIt) {
  EXPECT_FALSE(millis_reached(BEFORE_WRAP_MS, BEFORE_WRAP_MS + ACROSS_WRAP_MS));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
