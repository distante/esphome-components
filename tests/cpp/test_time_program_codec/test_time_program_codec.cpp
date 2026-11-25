#include <gtest/gtest.h>

#include <string>

#include "sec_touch/fan_level.h"
#include "sec_touch/time_program_codec.h"
#include "sec_touch/time_program_write.h"

using esphome::sec_touch::AUTOMATIC_CO2_LEVEL;
using esphome::sec_touch::AUTOMATIC_CO2_NAME;
using esphome::sec_touch::AUTOMATIC_TIME_LEVEL;
using esphome::sec_touch::format_enabled_mask;
using esphome::sec_touch::format_switch_time_level;
using esphome::sec_touch::format_switch_time_time;
using esphome::sec_touch::MAX_SWITCH_TIME_LEVEL;
using esphome::sec_touch::minute_of_day;
using esphome::sec_touch::NOT_CONNECTED_LEVEL;
using esphome::sec_touch::OFF_LEVEL;
using esphome::sec_touch::OFF_NAME;
using esphome::sec_touch::parse_enabled_mask;
using esphome::sec_touch::parse_switch_time_level;
using esphome::sec_touch::parse_switch_time_time;
using esphome::sec_touch::set_write_failure;
using esphome::sec_touch::SLEEP_LEVEL;
using esphome::sec_touch::SWITCH_TIME_COUNT;
using esphome::sec_touch::switch_time_bit;
using esphome::sec_touch::SWITCH_TIME_LEVEL_OPTION_COUNT;
using esphome::sec_touch::SWITCH_TIME_LEVEL_OPTIONS;
using esphome::sec_touch::TaskEnd;
using esphome::sec_touch::write_failure_name;
using esphome::sec_touch::WriteFailure;

namespace {

constexpr int UNEXPECTED = -1;
constexpr int FIRST_SWITCH_TIME = 1;
constexpr int LAST_SWITCH_TIME = SWITCH_TIME_COUNT;

std::string text(const char *buffer) { return std::string(buffer); }

}  // namespace

TEST(SwitchTimeTime, ReadsHoursMinutesAndSeconds) {
  auto time = parse_switch_time_time("08:30:15");
  ASSERT_TRUE(time.has_value());
  EXPECT_EQ(time->hour, 8);
  EXPECT_EQ(time->minute, 30);
  EXPECT_EQ(time->second, 15);
}

// Same trim as the meaning rule.
TEST(SwitchTimeTime, ReadsATrimmedTime) { EXPECT_TRUE(parse_switch_time_time(" 23:59:59 ").has_value()); }

TEST(SwitchTimeTime, AnythingTheMeaningRuleMarksUnexpectedIsNoTime) {
  EXPECT_FALSE(parse_switch_time_time("24:00:00").has_value());
  EXPECT_FALSE(parse_switch_time_time("08:60:00").has_value());
  EXPECT_FALSE(parse_switch_time_time("08:30").has_value());
  EXPECT_FALSE(parse_switch_time_time("").has_value());
}

// AC 8: the seconds are dropped, never rounded.
TEST(SwitchTimeTime, IsWrittenWithZeroSeconds) {
  EXPECT_EQ(text(format_switch_time_time(8, 30).data()), "08:30:00");
  EXPECT_EQ(text(format_switch_time_time(23, 59).data()), "23:59:00");
  EXPECT_EQ(text(format_switch_time_time(0, 0).data()), "00:00:00");
}

TEST(SwitchTimeTime, TimesCompareByMinuteOfDay) {
  EXPECT_EQ(minute_of_day(8, 30), minute_of_day(8, 30));
  EXPECT_NE(minute_of_day(8, 30), minute_of_day(8, 31));
  EXPECT_NE(minute_of_day(8, 30), minute_of_day(9, 30));
}

TEST(SwitchTimeLevel, ReadsEveryLevelASwitchTimeTakes) {
  for (int level = OFF_LEVEL; level <= MAX_SWITCH_TIME_LEVEL; level++) {
    EXPECT_EQ(parse_switch_time_level(std::to_string(level)), level);
  }
}

// AC 8 and the meaning rule: 10, 11 and 255 are unexpected.
TEST(SwitchTimeLevel, LevelsAboveTheSwitchTimeLevelsAreUnexpected) {
  EXPECT_EQ(parse_switch_time_level(std::to_string(AUTOMATIC_TIME_LEVEL)), UNEXPECTED);
  EXPECT_EQ(parse_switch_time_level(std::to_string(SLEEP_LEVEL)), UNEXPECTED);
  EXPECT_EQ(parse_switch_time_level(std::to_string(NOT_CONNECTED_LEVEL)), UNEXPECTED);
  EXPECT_EQ(parse_switch_time_level("x"), UNEXPECTED);
  EXPECT_EQ(parse_switch_time_level(""), UNEXPECTED);
}

TEST(SwitchTimeLevel, IsWrittenAsItsDecimal) {
  for (int level = OFF_LEVEL; level <= MAX_SWITCH_TIME_LEVEL; level++) {
    EXPECT_EQ(text(format_switch_time_level(level).data()), std::to_string(level));
  }
}

// The select's option at index i is level i's name.
TEST(SwitchTimeLevel, SelectOptionsAreTheLevelNamesByLevel) {
  EXPECT_EQ(SWITCH_TIME_LEVEL_OPTION_COUNT, (size_t) (MAX_SWITCH_TIME_LEVEL - OFF_LEVEL + 1));
  EXPECT_EQ(text(SWITCH_TIME_LEVEL_OPTIONS[OFF_LEVEL]), OFF_NAME);
  EXPECT_EQ(text(SWITCH_TIME_LEVEL_OPTIONS[AUTOMATIC_CO2_LEVEL]), AUTOMATIC_CO2_NAME);
}

TEST(EnabledMask, ReadsTwoHexDigitsInEitherCase) {
  EXPECT_EQ(parse_enabled_mask("00"), 0);
  EXPECT_EQ(parse_enabled_mask("1f"), switch_time_bit(LAST_SWITCH_TIME + 1) - 1);
  EXPECT_EQ(parse_enabled_mask(" 0B "), switch_time_bit(1) | switch_time_bit(2) | switch_time_bit(4));
}

// A bit above switch time 5's makes the mask unexpected.
TEST(EnabledMask, AMaskWithABitAboveTheSwitchTimesIsUnexpected) {
  EXPECT_EQ(parse_enabled_mask("20"), UNEXPECTED);
  EXPECT_EQ(parse_enabled_mask("1"), UNEXPECTED);
  EXPECT_EQ(parse_enabled_mask("zz"), UNEXPECTED);
}

TEST(EnabledMask, IsWrittenAsTwoUppercaseHexDigits) {
  EXPECT_EQ(text(format_enabled_mask(0).data()), "00");
  EXPECT_EQ(text(format_enabled_mask(switch_time_bit(1) | switch_time_bit(2) | switch_time_bit(4)).data()), "0B");
  EXPECT_EQ(text(format_enabled_mask(switch_time_bit(LAST_SWITCH_TIME + 1) - 1).data()), "1F");
}

TEST(EnabledMask, SwitchTimeNHasBitNMinus1) {
  EXPECT_EQ(switch_time_bit(FIRST_SWITCH_TIME), 0x01);
  EXPECT_EQ(switch_time_bit(LAST_SWITCH_TIME), 0x10);
}

TEST(WriteFailure, ASetsNakIsRefusedAndItsTimeoutATimeout) {
  EXPECT_EQ(set_write_failure(TaskEnd::TASK_END_NAK), WriteFailure::REFUSED);
  EXPECT_EQ(set_write_failure(TaskEnd::TASK_END_TIMEOUT), WriteFailure::TIMEOUT);
}

// A corrupted ACK leaves it to the read-back (decisions: no write_failed for the SET).
TEST(WriteFailure, AnAnsweredOrMalformedSetIsNoFailure) {
  EXPECT_FALSE(set_write_failure(TaskEnd::TASK_END_ANSWERED).has_value());
  EXPECT_FALSE(set_write_failure(TaskEnd::TASK_END_MALFORMED).has_value());
}

// The event types of the spec's table.
TEST(WriteFailure, NamesAreTheEventTypes) {
  EXPECT_EQ(text(write_failure_name(WriteFailure::REFUSED)), "refused");
  EXPECT_EQ(text(write_failure_name(WriteFailure::TIMEOUT)), "timeout");
  EXPECT_EQ(text(write_failure_name(WriteFailure::NOT_KEPT)), "not_kept");
  EXPECT_EQ(text(write_failure_name(WriteFailure::MASK_UNREADABLE)), "mask_unreadable");
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
