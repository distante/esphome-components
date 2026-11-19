#include <gtest/gtest.h>

#include "sec_touch/fan/fan_control.h"

using esphome::sec_touch::AUTOMATIC_CO2_LEVEL;
using esphome::sec_touch::BURST_LEVEL;
using esphome::sec_touch::decide_fan_control;
using esphome::sec_touch::FanCommand;
using esphome::sec_touch::FanControl;
using esphome::sec_touch::FanStatus;
using esphome::sec_touch::MAX_NORMAL_LEVEL;
using esphome::sec_touch::MIN_NORMAL_LEVEL;
using esphome::sec_touch::NORMAL_PRESET_LEVEL;
using esphome::sec_touch::OFF_LEVEL;
using esphome::sec_touch::SLEEP_LEVEL;

namespace {
constexpr bool NOT_SPLIT = false;
constexpr bool SPLIT = true;

const FanStatus OFF_AT_3{/*on=*/false, /*speed=*/3};
const FanStatus ON_AT_4{/*on=*/true, /*speed=*/4};
}  // namespace

// Issue #54: Home Assistant sent "State: OFF Speed: 1" to an off fan and the panel turned it on.
TEST(DecideFanControl, OffWithASpeedLeavesAnOffFanOff) {
  FanControl result = decide_fan_control(FanCommand{false, 1, {}}, OFF_AT_3, NOT_SPLIT);

  EXPECT_EQ(result.level, OFF_LEVEL);
  EXPECT_FALSE(result.on);
}

TEST(DecideFanControl, OffTurnsARunningFanOff) {
  FanControl result = decide_fan_control(FanCommand{false, {}, {}}, ON_AT_4, NOT_SPLIT);

  EXPECT_EQ(result.level, OFF_LEVEL);
  EXPECT_FALSE(result.on);
}

TEST(DecideFanControl, OffWithASpeedTurnsARunningFanOff) {
  FanControl result = decide_fan_control(FanCommand{false, 2, {}}, ON_AT_4, NOT_SPLIT);

  EXPECT_EQ(result.level, OFF_LEVEL);
  EXPECT_FALSE(result.on);
}

// The fan may be on at the panel even though ESPHome thinks it is off, so OFF is sent anyway.
TEST(DecideFanControl, OffOnAnOffFanStillWritesLevelZero) {
  FanControl result = decide_fan_control(FanCommand{false, {}, {}}, OFF_AT_3, NOT_SPLIT);

  EXPECT_EQ(result.level, OFF_LEVEL);
  EXPECT_FALSE(result.on);
}

// An OFF that also picks a preset (the hub stores the preset) still turns the fan off.
TEST(DecideFanControl, OffWithAPresetLeavesAnOffFanOff) {
  FanControl result = decide_fan_control(FanCommand{false, {}, BURST_LEVEL}, OFF_AT_3, NOT_SPLIT);

  EXPECT_EQ(result.level, OFF_LEVEL);
  EXPECT_FALSE(result.on);
}

TEST(DecideFanControl, OnWithoutASpeedStartsAFanWithNoStoredSpeedAtOne) {
  FanControl result = decide_fan_control(FanCommand{true, {}, {}}, FanStatus{false, 0}, NOT_SPLIT);

  EXPECT_EQ(result.level, MIN_NORMAL_LEVEL);
  EXPECT_TRUE(result.on);
  EXPECT_EQ(result.speed, MIN_NORMAL_LEVEL);
}

TEST(DecideFanControl, ASpecialPresetWritesItsLevelAndShowsItAsTheSpeed) {
  FanControl result = decide_fan_control(FanCommand{{}, {}, BURST_LEVEL}, ON_AT_4, NOT_SPLIT);

  EXPECT_EQ(result.level, BURST_LEVEL);
  EXPECT_TRUE(result.on);
  EXPECT_EQ(result.speed, BURST_LEVEL);
}

TEST(DecideFanControl, SplitBurstWritesItsLevelAndShowsTheSliderAtMax) {
  FanControl result = decide_fan_control(FanCommand{{}, {}, BURST_LEVEL}, ON_AT_4, SPLIT);

  EXPECT_EQ(result.level, BURST_LEVEL);
  EXPECT_EQ(result.speed, MAX_NORMAL_LEVEL);
}

TEST(DecideFanControl, SplitSleepWritesItsLevelAndLeavesTheSliderAlone) {
  FanControl result = decide_fan_control(FanCommand{{}, {}, SLEEP_LEVEL}, ON_AT_4, SPLIT);

  EXPECT_EQ(result.level, SLEEP_LEVEL);
  EXPECT_EQ(result.speed, 4);
}

TEST(DecideFanControl, NormalPresetRestartsAtLevelOne) {
  FanControl result = decide_fan_control(FanCommand{{}, {}, NORMAL_PRESET_LEVEL}, ON_AT_4, NOT_SPLIT);

  EXPECT_EQ(result.level, MIN_NORMAL_LEVEL);
  EXPECT_EQ(result.speed, MIN_NORMAL_LEVEL);
}

TEST(DecideFanControl, SplitNormalPresetKeepsTheSliderSpeed) {
  FanControl result = decide_fan_control(FanCommand{{}, {}, NORMAL_PRESET_LEVEL}, ON_AT_4, SPLIT);

  EXPECT_EQ(result.level, 4);
  EXPECT_EQ(result.speed, 4);
}

// Leaving Sleep in split mode can find the slider at a special level, outside the normal levels.
TEST(DecideFanControl, SplitNormalPresetPullsAnOutOfRangeSliderBackToOne) {
  FanControl result = decide_fan_control(FanCommand{{}, {}, NORMAL_PRESET_LEVEL}, FanStatus{true, SLEEP_LEVEL}, SPLIT);

  EXPECT_EQ(result.level, MIN_NORMAL_LEVEL);
  EXPECT_EQ(result.speed, MIN_NORMAL_LEVEL);
}

TEST(DecideFanControl, ASpeedWithoutAPresetSetsTheLevelAndResetsThePresetToNormal) {
  FanControl result = decide_fan_control(FanCommand{{}, 5, {}}, ON_AT_4, NOT_SPLIT);

  EXPECT_EQ(result.level, 5);
  EXPECT_TRUE(result.on);
  EXPECT_EQ(result.speed, 5);
  EXPECT_TRUE(result.reset_preset_to_normal);
}

TEST(DecideFanControl, OnWithASpeedStartsAnOffFanAtThatSpeed) {
  FanControl result = decide_fan_control(FanCommand{true, 2, {}}, OFF_AT_3, NOT_SPLIT);

  EXPECT_EQ(result.level, 2);
  EXPECT_TRUE(result.on);
  EXPECT_EQ(result.speed, 2);
  EXPECT_TRUE(result.reset_preset_to_normal);
}

TEST(DecideFanControl, OnWithoutASpeedResumesTheStoredSpeed) {
  FanControl result = decide_fan_control(FanCommand{true, {}, {}}, OFF_AT_3, SPLIT);

  EXPECT_EQ(result.level, 3);
  EXPECT_TRUE(result.on);
  EXPECT_EQ(result.speed, 3);
}

// A special level from the panel is left alone: only normal levels mean Normal.
TEST(DecideFanControl, ResendingASpecialLevelDoesNotResetThePreset) {
  FanControl result = decide_fan_control(FanCommand{true, {}, {}}, FanStatus{true, AUTOMATIC_CO2_LEVEL}, NOT_SPLIT);

  EXPECT_EQ(result.level, AUTOMATIC_CO2_LEVEL);
  EXPECT_FALSE(result.reset_preset_to_normal);
}

// The hub already stored the preset the command picked.
TEST(DecideFanControl, APresetCommandLeavesThePresetAlone) {
  EXPECT_FALSE(
      decide_fan_control(FanCommand{{}, {}, NORMAL_PRESET_LEVEL}, ON_AT_4, NOT_SPLIT).reset_preset_to_normal);
  EXPECT_FALSE(decide_fan_control(FanCommand{{}, {}, BURST_LEVEL}, ON_AT_4, SPLIT).reset_preset_to_normal);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
