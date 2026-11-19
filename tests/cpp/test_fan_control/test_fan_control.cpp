#include <gtest/gtest.h>

#include "sec_touch/fan/fan_control.h"

using esphome::sec_touch::decide_fan_control;
using esphome::sec_touch::FanCommand;
using esphome::sec_touch::FanControl;
using esphome::sec_touch::FanStatus;

namespace {
constexpr bool NOT_SPLIT = false;
constexpr bool SPLIT = true;

const FanStatus OFF_AT_3{/*on=*/false, /*speed=*/3};
const FanStatus ON_AT_4{/*on=*/true, /*speed=*/4};

// Panel levels of the presets (FanModeEnum's start speeds).
constexpr int NORMAL = 0;
constexpr int BURST = 7;
constexpr int SLEEP = 11;
}  // namespace

// Issue #54: Home Assistant sent "State: OFF Speed: 1" to an off fan and the panel turned it on.
TEST(DecideFanControl, OffWithASpeedLeavesAnOffFanOff) {
  FanControl result = decide_fan_control(FanCommand{false, 1, {}}, OFF_AT_3, NOT_SPLIT);

  EXPECT_EQ(result.level, 0);
  EXPECT_FALSE(result.on);
}

TEST(DecideFanControl, OffTurnsARunningFanOff) {
  FanControl result = decide_fan_control(FanCommand{false, {}, {}}, ON_AT_4, NOT_SPLIT);

  EXPECT_EQ(result.level, 0);
  EXPECT_FALSE(result.on);
}

TEST(DecideFanControl, OffWithASpeedTurnsARunningFanOff) {
  FanControl result = decide_fan_control(FanCommand{false, 2, {}}, ON_AT_4, NOT_SPLIT);

  EXPECT_EQ(result.level, 0);
  EXPECT_FALSE(result.on);
}

// The fan may be on at the panel even though ESPHome thinks it is off, so OFF is sent anyway.
TEST(DecideFanControl, OffOnAnOffFanStillWritesLevelZero) {
  FanControl result = decide_fan_control(FanCommand{false, {}, {}}, OFF_AT_3, NOT_SPLIT);

  EXPECT_EQ(result.level, 0);
  EXPECT_FALSE(result.on);
}

// An OFF that also picks a preset (the hub stores the preset) still turns the fan off.
TEST(DecideFanControl, OffWithAPresetLeavesAnOffFanOff) {
  FanControl result = decide_fan_control(FanCommand{false, {}, BURST}, OFF_AT_3, NOT_SPLIT);

  EXPECT_EQ(result.level, 0);
  EXPECT_FALSE(result.on);
}

TEST(DecideFanControl, OnWithoutASpeedStartsAFanWithNoStoredSpeedAtOne) {
  FanControl result = decide_fan_control(FanCommand{true, {}, {}}, FanStatus{false, 0}, NOT_SPLIT);

  EXPECT_EQ(result.level, 1);
  EXPECT_TRUE(result.on);
  EXPECT_EQ(result.speed, 1);
}

TEST(DecideFanControl, ASpecialPresetWritesItsLevelAndShowsItAsTheSpeed) {
  FanControl result = decide_fan_control(FanCommand{{}, {}, BURST}, ON_AT_4, NOT_SPLIT);

  EXPECT_EQ(result.level, 7);
  EXPECT_TRUE(result.on);
  EXPECT_EQ(result.speed, 7);
}

TEST(DecideFanControl, SplitBurstWritesItsLevelAndShowsTheSliderAtMax) {
  FanControl result = decide_fan_control(FanCommand{{}, {}, BURST}, ON_AT_4, SPLIT);

  EXPECT_EQ(result.level, 7);
  EXPECT_EQ(result.speed, 6);
}

TEST(DecideFanControl, SplitSleepWritesItsLevelAndLeavesTheSliderAlone) {
  FanControl result = decide_fan_control(FanCommand{{}, {}, SLEEP}, ON_AT_4, SPLIT);

  EXPECT_EQ(result.level, 11);
  EXPECT_EQ(result.speed, 4);
}

TEST(DecideFanControl, NormalPresetRestartsAtLevelOne) {
  FanControl result = decide_fan_control(FanCommand{{}, {}, NORMAL}, ON_AT_4, NOT_SPLIT);

  EXPECT_EQ(result.level, 1);
  EXPECT_EQ(result.speed, 1);
}

TEST(DecideFanControl, SplitNormalPresetKeepsTheSliderSpeed) {
  FanControl result = decide_fan_control(FanCommand{{}, {}, NORMAL}, ON_AT_4, SPLIT);

  EXPECT_EQ(result.level, 4);
  EXPECT_EQ(result.speed, 4);
}

// Leaving Sleep in split mode can find the slider at a special level, outside the 1..6 range.
TEST(DecideFanControl, SplitNormalPresetPullsAnOutOfRangeSliderBackToOne) {
  FanControl result = decide_fan_control(FanCommand{{}, {}, NORMAL}, FanStatus{true, 11}, SPLIT);

  EXPECT_EQ(result.level, 1);
  EXPECT_EQ(result.speed, 1);
}

TEST(DecideFanControl, ASpeedWithoutAPresetSetsTheLevelAndMarksThePresetNormal) {
  FanControl result = decide_fan_control(FanCommand{{}, 5, {}}, ON_AT_4, NOT_SPLIT);

  EXPECT_EQ(result.level, 5);
  EXPECT_TRUE(result.on);
  EXPECT_EQ(result.speed, 5);
  EXPECT_TRUE(result.preset_is_normal);
}

TEST(DecideFanControl, OnWithASpeedStartsAnOffFanAtThatSpeed) {
  FanControl result = decide_fan_control(FanCommand{true, 2, {}}, OFF_AT_3, NOT_SPLIT);

  EXPECT_EQ(result.level, 2);
  EXPECT_TRUE(result.on);
  EXPECT_EQ(result.speed, 2);
  EXPECT_TRUE(result.preset_is_normal);
}

TEST(DecideFanControl, OnWithoutASpeedResumesTheStoredSpeed) {
  FanControl result = decide_fan_control(FanCommand{true, {}, {}}, OFF_AT_3, SPLIT);

  EXPECT_EQ(result.level, 3);
  EXPECT_TRUE(result.on);
  EXPECT_EQ(result.speed, 3);
}

// A special level from the panel is left alone: only levels 1..6 mean Normal.
TEST(DecideFanControl, ResendingASpecialLevelDoesNotMarkThePresetNormal) {
  FanControl result = decide_fan_control(FanCommand{true, {}, {}}, FanStatus{true, 9}, NOT_SPLIT);

  EXPECT_EQ(result.level, 9);
  EXPECT_FALSE(result.preset_is_normal);
}

// The hub already stored the preset the command picked.
TEST(DecideFanControl, APresetCommandLeavesThePresetAlone) {
  EXPECT_FALSE(decide_fan_control(FanCommand{{}, {}, NORMAL}, ON_AT_4, NOT_SPLIT).preset_is_normal);
  EXPECT_FALSE(decide_fan_control(FanCommand{{}, {}, BURST}, ON_AT_4, SPLIT).preset_is_normal);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
