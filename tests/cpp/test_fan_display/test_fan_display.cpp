#include <gtest/gtest.h>

#include <optional>

#include "../common/fan_status.h"
#include "sec_touch/fan/fan_display.h"

using esphome::sec_touch::AUTOMATIC_CO2_LEVEL;
using esphome::sec_touch::AUTOMATIC_HUMIDITY_LEVEL;
using esphome::sec_touch::AUTOMATIC_TIME_LEVEL;
using esphome::sec_touch::BURST_LEVEL;
using esphome::sec_touch::decide_fan_display;
using esphome::sec_touch::FanStatus;
using esphome::sec_touch::MAX_NORMAL_LEVEL;
using esphome::sec_touch::MIN_NORMAL_LEVEL;
using esphome::sec_touch::NOT_CONNECTED_LEVEL;
using esphome::sec_touch::OFF_LEVEL;
using esphome::sec_touch::SLEEP_LEVEL;
using sec_touch_test::NOT_SPLIT;
using sec_touch_test::OFF_AT_3;
using sec_touch_test::ON_AT_4;
using sec_touch_test::SPLIT;

namespace {

/// Before the first publish ESPHome holds speed 0, outside the normal levels.
const FanStatus BOOT{/*on=*/false, /*speed=*/0};

void expect_status(const FanStatus &actual, const FanStatus &expected) {
  EXPECT_EQ(actual.on, expected.on);
  EXPECT_EQ(actual.speed, expected.speed);
}

/// The fan's display with and without split_special_modes.
class FanDisplay : public ::testing::TestWithParam<bool> {
 protected:
  bool split() const { return this->GetParam(); }

  /// What a fan shows in Automatik Zeit, from OFF_AT_3, when it doesn't follow the running level: on, with the
  /// slider kept in split mode and at the level itself without it.
  FanStatus automatic_time_without_following() const {
    return this->split() ? FanStatus{true, OFF_AT_3.speed} : FanStatus{true, AUTOMATIC_TIME_LEVEL};
  }
};

}  // namespace

INSTANTIATE_TEST_SUITE_P(SplitSpecialModes, FanDisplay, ::testing::Values(NOT_SPLIT, SPLIT),
                         [](const ::testing::TestParamInfo<bool> &info) { return info.param ? "Split" : "NotSplit"; });

// ----- Automatik Zeit, following the running level (.specs/time-programs.md, "Live state") -----

TEST_P(FanDisplay, RunningLevelOffShowsOffWithTheSpeedUnchanged) {
  expect_status(decide_fan_display(AUTOMATIC_TIME_LEVEL, OFF_LEVEL, ON_AT_4, this->split()),
                FanStatus{false, ON_AT_4.speed});
}

TEST_P(FanDisplay, RunningLevelOffKeepsAnOffFanOff) {
  expect_status(decide_fan_display(AUTOMATIC_TIME_LEVEL, OFF_LEVEL, OFF_AT_3, this->split()), OFF_AT_3);
}

TEST_P(FanDisplay, EachNormalRunningLevelShowsOnAtThatLevel) {
  for (int level = MIN_NORMAL_LEVEL; level <= MAX_NORMAL_LEVEL; level++) {
    SCOPED_TRACE(level);
    expect_status(decide_fan_display(AUTOMATIC_TIME_LEVEL, level, OFF_AT_3, this->split()), FanStatus{true, level});
  }
}

TEST_P(FanDisplay, ANormalRunningLevelSetsTheSpeedFromBoot) {
  expect_status(decide_fan_display(AUTOMATIC_TIME_LEVEL, MAX_NORMAL_LEVEL, BOOT, this->split()),
                FanStatus{true, MAX_NORMAL_LEVEL});
}

TEST_P(FanDisplay, ASpecialRunningLevelShowsAsWithoutFollowing) {
  for (int level : {BURST_LEVEL, AUTOMATIC_HUMIDITY_LEVEL, AUTOMATIC_CO2_LEVEL}) {
    SCOPED_TRACE(level);
    expect_status(decide_fan_display(AUTOMATIC_TIME_LEVEL, level, OFF_AT_3, this->split()),
                  this->automatic_time_without_following());
  }
}

// No running level: none, conflict, a needed input never read well, or a fan with follow_time_program: false.
TEST_P(FanDisplay, NoRunningLevelShowsAsWithoutFollowing) {
  expect_status(decide_fan_display(AUTOMATIC_TIME_LEVEL, std::nullopt, OFF_AT_3, this->split()),
                this->automatic_time_without_following());
}

TEST_P(FanDisplay, NoRunningLevelFromBootShowsANormalSliderInSplitMode) {
  const FanStatus expected = this->split() ? FanStatus{true, MIN_NORMAL_LEVEL} : FanStatus{true, AUTOMATIC_TIME_LEVEL};
  expect_status(decide_fan_display(AUTOMATIC_TIME_LEVEL, std::nullopt, BOOT, this->split()), expected);
}

// ----- Outside Automatik Zeit the running level is ignored -----

TEST_P(FanDisplay, ANormalLevelIgnoresTheRunningLevel) {
  expect_status(decide_fan_display(MIN_NORMAL_LEVEL, OFF_LEVEL, OFF_AT_3, this->split()),
                FanStatus{true, MIN_NORMAL_LEVEL});
}

TEST_P(FanDisplay, OffIgnoresTheRunningLevel) {
  expect_status(decide_fan_display(OFF_LEVEL, MAX_NORMAL_LEVEL, ON_AT_4, this->split()),
                FanStatus{false, ON_AT_4.speed});
}

TEST_P(FanDisplay, NotConnectedShowsOffAtItsLevel) {
  expect_status(decide_fan_display(NOT_CONNECTED_LEVEL, std::nullopt, ON_AT_4, this->split()),
                FanStatus{false, NOT_CONNECTED_LEVEL});
}

TEST_P(FanDisplay, ANormalLevelShowsOnAtThatLevel) {
  expect_status(decide_fan_display(MAX_NORMAL_LEVEL, std::nullopt, OFF_AT_3, this->split()),
                FanStatus{true, MAX_NORMAL_LEVEL});
}

// ----- Special levels without following -----

TEST(FanDisplaySpecial, WithoutSplitASpecialLevelIsTheSpeed) {
  expect_status(decide_fan_display(SLEEP_LEVEL, std::nullopt, ON_AT_4, NOT_SPLIT), FanStatus{true, SLEEP_LEVEL});
}

TEST(FanDisplaySpecial, InSplitModeBurstPutsTheSliderAtMax) {
  expect_status(decide_fan_display(BURST_LEVEL, std::nullopt, ON_AT_4, SPLIT), FanStatus{true, MAX_NORMAL_LEVEL});
}

TEST(FanDisplaySpecial, InSplitModeAnotherSpecialLevelKeepsANormalSlider) {
  expect_status(decide_fan_display(SLEEP_LEVEL, std::nullopt, ON_AT_4, SPLIT), FanStatus{true, ON_AT_4.speed});
}

TEST(FanDisplaySpecial, InSplitModeAnotherSpecialLevelMovesASliderOutsideTheNormalLevels) {
  const FanStatus not_connected{false, NOT_CONNECTED_LEVEL};
  expect_status(decide_fan_display(SLEEP_LEVEL, std::nullopt, not_connected, SPLIT), FanStatus{true, MIN_NORMAL_LEVEL});
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
