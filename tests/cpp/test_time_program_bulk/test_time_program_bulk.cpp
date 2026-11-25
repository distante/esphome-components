#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sec_touch/time_program_bulk.h"

using esphome::sec_touch::BULK_BUSY;
using esphome::sec_touch::BULK_INVALID_ARGUMENT;
using esphome::sec_touch::bulk_unlisted_pair_error;
using esphome::sec_touch::BulkProgram;
using esphome::sec_touch::BulkWrite;
using esphome::sec_touch::MAX_SWITCH_TIME_LEVEL;
using esphome::sec_touch::minute_of_day;
using esphome::sec_touch::OFF_LEVEL;
using esphome::sec_touch::parse_bulk_time;
using esphome::sec_touch::read_bulk_program;
using esphome::sec_touch::SWITCH_TIME_COUNT;
using esphome::sec_touch::switch_time_bit;
using esphome::sec_touch::TaskEnd;
using esphome::sec_touch::TimeProgramField;
using esphome::sec_touch::TimeProgramSlot;
using esphome::sec_touch::WriteFailure;

namespace {

constexpr int PAIR = 1;

/// The spec's HA call: a valid program.
std::vector<std::string> good_times() { return {"06:00", "08:30", "17:00", "22:00", "00:00"}; }
std::vector<int32_t> good_levels() { return {4, 2, 4, 1, 0}; }
std::vector<bool> good_enabled() { return {true, true, true, true, false}; }

std::string invalid(const std::string &detail) { return std::string(BULK_INVALID_ARGUMENT) + ": " + detail; }

TimeProgramSlot time_slot(int n) { return TimeProgramSlot{PAIR, n, TimeProgramField::TIME}; }
TimeProgramSlot level_slot(int n) { return TimeProgramSlot{PAIR, n, TimeProgramField::LEVEL}; }
TimeProgramSlot mask_slot() { return TimeProgramSlot{PAIR, 0, TimeProgramField::ENABLED}; }

BulkProgram good_program() {
  BulkProgram program;
  EXPECT_FALSE(read_bulk_program(good_times(), good_levels(), good_enabled(), program).has_value());
  return program;
}

}  // namespace

TEST(BulkTime, ReadsHoursAndMinutes) {
  auto time = parse_bulk_time("08:30");
  ASSERT_TRUE(time.has_value());
  EXPECT_EQ(time->hour, 8);
  EXPECT_EQ(time->minute, 30);
}

// Spec: HH:MM:SS is accepted; its seconds are dropped by the write.
TEST(BulkTime, ReadsATimeWithSeconds) {
  auto time = parse_bulk_time("23:59:59");
  ASSERT_TRUE(time.has_value());
  EXPECT_EQ(time->hour, 23);
  EXPECT_EQ(time->minute, 59);
}

TEST(BulkTime, RejectsAnythingElse) {
  EXPECT_FALSE(parse_bulk_time("24:00").has_value());
  EXPECT_FALSE(parse_bulk_time("08:60").has_value());
  EXPECT_FALSE(parse_bulk_time("8:30").has_value());
  EXPECT_FALSE(parse_bulk_time("08:30:60").has_value());
  EXPECT_FALSE(parse_bulk_time("08-30").has_value());
  EXPECT_FALSE(parse_bulk_time("").has_value());
}

TEST(BulkProgramArguments, AValidProgramIsRead) {
  BulkProgram program;
  EXPECT_EQ(read_bulk_program(good_times(), good_levels(), good_enabled(), program), std::nullopt);

  EXPECT_EQ(program.times[1].hour, 8);
  EXPECT_EQ(program.times[1].minute, 30);
  EXPECT_EQ(program.levels[0], 4);
  EXPECT_EQ(program.levels[4], 0);
  EXPECT_EQ(program.enabled, switch_time_bit(1) | switch_time_bit(2) | switch_time_bit(3) | switch_time_bit(4));
}

// The spec's example, verbatim.
TEST(BulkProgramArguments, ALevelOutOfRangeNamesItsSwitchTime) {
  BulkProgram program;
  std::vector<int32_t> levels = good_levels();
  levels[3] = 12;

  EXPECT_EQ(read_bulk_program(good_times(), levels, good_enabled(), program),
            "invalid_argument: switch time 4 level 12 not in 0–9");
}

TEST(BulkProgramArguments, LevelsAcceptTheirWholeRange) {
  BulkProgram program;
  std::vector<int32_t> levels = good_levels();
  levels[0] = OFF_LEVEL;
  levels[1] = MAX_SWITCH_TIME_LEVEL;
  EXPECT_EQ(read_bulk_program(good_times(), levels, good_enabled(), program), std::nullopt);

  levels[1] = OFF_LEVEL - 1;
  EXPECT_EQ(read_bulk_program(good_times(), levels, good_enabled(), program),
            invalid("switch time 2 level -1 not in 0–9"));
}

TEST(BulkProgramArguments, ABadTimeNamesItsSwitchTime) {
  BulkProgram program;
  std::vector<std::string> times = good_times();
  times[1] = "25:00";

  EXPECT_EQ(read_bulk_program(times, good_levels(), good_enabled(), program),
            invalid("switch time 2 time '25:00' not HH:MM or HH:MM:SS"));
}

TEST(BulkProgramArguments, EachListNeedsOneValuePerSwitchTime) {
  BulkProgram program;
  std::vector<std::string> times = good_times();
  times.pop_back();
  EXPECT_EQ(read_bulk_program(times, good_levels(), good_enabled(), program), invalid("times has 4 values, not 5"));

  std::vector<int32_t> levels = good_levels();
  levels.push_back(0);
  EXPECT_EQ(read_bulk_program(good_times(), levels, good_enabled(), program), invalid("levels has 6 values, not 5"));

  EXPECT_EQ(read_bulk_program(good_times(), good_levels(), {}, program), invalid("enabled has 0 values, not 5"));
}

TEST(BulkProgramArguments, AnUnlistedPairIsAnInvalidArgument) {
  EXPECT_EQ(bulk_unlisted_pair_error(7), invalid("fan_number 7 not in time_programs"));
}

TEST(BulkWriteReport, IsIdleUntilBegun) {
  BulkWrite bulk;
  EXPECT_FALSE(bulk.active());
  EXPECT_FALSE(bulk.done());
}

/// Ends every write of `bulk` with a read-back equal to what `program` sent.
void every_write_lands(BulkWrite &bulk, const BulkProgram &program) {
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    bulk.set_ended(time_slot(n), true);
    bulk.read_back(time_slot(n), minute_of_day(program.times[n - 1].hour, program.times[n - 1].minute));
    bulk.set_ended(level_slot(n), true);
    bulk.read_back(level_slot(n), program.levels[n - 1]);
  }
  bulk.set_ended(mask_slot(), true);
  bulk.read_back(mask_slot(), program.enabled);
}

TEST(BulkWriteReport, SucceedsWhenEveryReadBackMatches) {
  BulkWrite bulk;
  const BulkProgram program = good_program();
  bulk.begin(program);
  EXPECT_TRUE(bulk.active());

  every_write_lands(bulk, program);

  EXPECT_TRUE(bulk.done());
  EXPECT_EQ(bulk.finish(), std::nullopt);
  EXPECT_FALSE(bulk.active());
}

TEST(BulkWriteReport, WaitsForEachReadBack) {
  BulkWrite bulk;
  const BulkProgram program = good_program();
  bulk.begin(program);
  every_write_lands(bulk, program);
  bulk.finish();
  bulk.begin(program);

  bulk.set_ended(mask_slot(), true);
  bulk.read_back(mask_slot(), program.enabled);
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    bulk.set_ended(time_slot(n), true);
    bulk.read_back(time_slot(n), minute_of_day(program.times[n - 1].hour, program.times[n - 1].minute));
    bulk.set_ended(level_slot(n), true);
  }

  EXPECT_FALSE(bulk.done());
}

// A read-back before the SET ended (a poll answer) doesn't count.
TEST(BulkWriteReport, AnAnswerBeforeTheSetEndedIsNoReadBack) {
  BulkWrite bulk;
  const BulkProgram program = good_program();
  bulk.begin(program);
  bulk.read_back(level_slot(1), program.levels[0] + 1);

  every_write_lands(bulk, program);

  EXPECT_EQ(bulk.finish(), std::nullopt);
}

// The spec's example, verbatim; the failures in write order, each named once with its first reason.
TEST(BulkWriteReport, ListsEachFailedWriteWithItsFirstReason) {
  BulkWrite bulk;
  const BulkProgram program = good_program();
  bulk.begin(program);

  bulk.fail(mask_slot(), WriteFailure::MASK_UNREADABLE);
  bulk.fail(level_slot(3), WriteFailure::REFUSED);
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    bulk.set_ended(time_slot(n), true);
    bulk.read_back(time_slot(n), minute_of_day(program.times[n - 1].hour, program.times[n - 1].minute));
    bulk.set_ended(level_slot(n), true);
    // Switch time 3's read-back differs too: still one failure, refused.
    bulk.read_back(level_slot(n), n == 3 ? program.levels[n - 1] + 1 : program.levels[n - 1]);
  }

  ASSERT_TRUE(bulk.done());
  EXPECT_EQ(bulk.finish(), "2 of 11 writes failed: switch time 3 level refused; enabled mask mask_unreadable");
}

TEST(BulkWriteReport, AReadBackThatDiffersIsNotKept) {
  BulkWrite bulk;
  const BulkProgram program = good_program();
  bulk.begin(program);
  every_write_lands(bulk, program);
  bulk.finish();
  bulk.begin(program);

  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    bulk.set_ended(time_slot(n), true);
    bulk.read_back(time_slot(n), n == 2 ? -1 : minute_of_day(program.times[n - 1].hour, program.times[n - 1].minute));
    bulk.set_ended(level_slot(n), true);
    bulk.read_back(level_slot(n), program.levels[n - 1]);
  }
  bulk.set_ended(mask_slot(), true);
  bulk.read_back(mask_slot(), program.enabled ^ switch_time_bit(5));

  EXPECT_EQ(bulk.finish(), "2 of 11 writes failed: switch time 2 time not_kept; enabled mask not_kept");
}

// A SET that ends with no read-back to follow (scan mode) ends its write; a read-back GET that fails leaves the
// write unconfirmed: a timeout.
TEST(BulkWriteReport, AWriteWithoutReadBackFails) {
  BulkWrite bulk;
  const BulkProgram program = good_program();
  bulk.begin(program);

  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    bulk.fail(time_slot(n), WriteFailure::TIMEOUT);
    bulk.set_ended(time_slot(n), false);
    bulk.set_ended(level_slot(n), true);
    bulk.get_ended(level_slot(n), TaskEnd::TASK_END_TIMEOUT);
  }
  bulk.fail(mask_slot(), WriteFailure::MASK_UNREADABLE);

  ASSERT_TRUE(bulk.done());
  EXPECT_EQ(
      bulk.finish(),
      "11 of 11 writes failed: switch time 1 time timeout; switch time 2 time timeout; switch time 3 time timeout; "
      "switch time 4 time timeout; switch time 5 time timeout; switch time 1 level timeout; switch time 2 level "
      "timeout; switch time 3 level timeout; switch time 4 level timeout; switch time 5 level timeout; enabled "
      "mask mask_unreadable");
}

// A GET answered but not compared (a newer SET was queued) can't confirm the bulk value.
TEST(BulkWriteReport, AReadBackAnsweredButNotComparedIsNotKept) {
  BulkWrite bulk;
  const BulkProgram program = good_program();
  bulk.begin(program);
  every_write_lands(bulk, program);
  bulk.finish();
  bulk.begin(program);

  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    bulk.set_ended(time_slot(n), true);
    bulk.read_back(time_slot(n), minute_of_day(program.times[n - 1].hour, program.times[n - 1].minute));
    bulk.set_ended(level_slot(n), true);
    if (n == 5) {
      bulk.get_ended(level_slot(n), TaskEnd::TASK_END_ANSWERED);
    } else {
      bulk.read_back(level_slot(n), program.levels[n - 1]);
    }
  }
  bulk.set_ended(mask_slot(), true);
  bulk.read_back(mask_slot(), program.enabled);

  EXPECT_EQ(bulk.finish(), "1 of 11 writes failed: switch time 5 level not_kept");
}

// The mask's fresh GET ends before its SET is queued: no read-back is due yet.
TEST(BulkWriteReport, TheFreshMaskGetIsNoReadBack) {
  BulkWrite bulk;
  const BulkProgram program = good_program();
  bulk.begin(program);
  bulk.get_ended(mask_slot(), TaskEnd::TASK_END_ANSWERED);

  every_write_lands(bulk, program);

  EXPECT_EQ(bulk.finish(), std::nullopt);
}

TEST(BulkWriteReport, BusyIsItsOwnError) { EXPECT_EQ(std::string(BULK_BUSY), "busy"); }

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
