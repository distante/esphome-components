#include <gtest/gtest.h>

#include <vector>

#include "sec_touch/time_program_ids.h"

using esphome::sec_touch::FAN_LEVEL_IDS;
using esphome::sec_touch::SWITCH_TIME_COUNT;
using esphome::sec_touch::TIME_PROGRAM_ENABLED_IDS;
using esphome::sec_touch::time_program_block_index;
using esphome::sec_touch::time_program_enabled_id;
using esphome::sec_touch::TIME_PROGRAM_IDS_PER_PAIR;
using esphome::sec_touch::TIME_PROGRAM_LEVEL_IDS;
using esphome::sec_touch::time_program_level_id;
using esphome::sec_touch::TIME_PROGRAM_PAIR_COUNT;
using esphome::sec_touch::time_program_slot;
using esphome::sec_touch::TIME_PROGRAM_TIME_IDS;
using esphome::sec_touch::time_program_time_id;
using esphome::sec_touch::TimeProgramField;
using esphome::sec_touch::TimeProgramPollList;

namespace {

constexpr int FIRST_PAIR = 1;
constexpr int LAST_PAIR = TIME_PROGRAM_PAIR_COUNT;
constexpr int FIRST_SWITCH_TIME = 1;
constexpr int LAST_SWITCH_TIME = SWITCH_TIME_COUNT;
constexpr int NOT_AN_ID = -1;
/// A fan level ID: known, but not part of any time program.
constexpr int FAN_1_LEVEL_ID = FAN_LEVEL_IDS[0];

}  // namespace

TEST(TimeProgramIds, EveryFanPairHasATimeProgram) { EXPECT_EQ(TIME_PROGRAM_PAIR_COUNT, (int) FAN_LEVEL_IDS.size()); }

TEST(TimeProgramIds, FirstAndLastPairsLookUpTheEndsOfTheArrays) {
  EXPECT_EQ(time_program_enabled_id(FIRST_PAIR), TIME_PROGRAM_ENABLED_IDS.front());
  EXPECT_EQ(time_program_enabled_id(LAST_PAIR), TIME_PROGRAM_ENABLED_IDS.back());
  EXPECT_EQ(time_program_time_id(FIRST_PAIR, FIRST_SWITCH_TIME), TIME_PROGRAM_TIME_IDS.front());
  EXPECT_EQ(time_program_time_id(LAST_PAIR, LAST_SWITCH_TIME), TIME_PROGRAM_TIME_IDS.back());
  EXPECT_EQ(time_program_level_id(FIRST_PAIR, FIRST_SWITCH_TIME), TIME_PROGRAM_LEVEL_IDS.front());
  EXPECT_EQ(time_program_level_id(LAST_PAIR, LAST_SWITCH_TIME), TIME_PROGRAM_LEVEL_IDS.back());
}

// Pair 2, switch time 3: the 3rd entry of pair 2's run of SWITCH_TIME_COUNT IDs.
TEST(TimeProgramIds, APairsSwitchTimeIsInThatPairsRun) {
  EXPECT_EQ(time_program_time_id(2, 3), TIME_PROGRAM_TIME_IDS[SWITCH_TIME_COUNT + 2]);
  EXPECT_EQ(time_program_level_id(2, 3), TIME_PROGRAM_LEVEL_IDS[SWITCH_TIME_COUNT + 2]);
}

TEST(TimeProgramIds, OutOfRangePairOrSwitchTimeHasNoId) {
  EXPECT_EQ(time_program_enabled_id(FIRST_PAIR - 1), NOT_AN_ID);
  EXPECT_EQ(time_program_enabled_id(LAST_PAIR + 1), NOT_AN_ID);
  EXPECT_EQ(time_program_time_id(FIRST_PAIR, FIRST_SWITCH_TIME - 1), NOT_AN_ID);
  EXPECT_EQ(time_program_time_id(FIRST_PAIR, LAST_SWITCH_TIME + 1), NOT_AN_ID);
  EXPECT_EQ(time_program_level_id(LAST_PAIR + 1, FIRST_SWITCH_TIME), NOT_AN_ID);
  EXPECT_EQ(time_program_level_id(FIRST_PAIR, LAST_SWITCH_TIME + 1), NOT_AN_ID);
}

// The reverse lookup leads every ID back to the pair, switch time and field it was looked up with.
TEST(TimeProgramIds, EveryIdLeadsBackToItsSlot) {
  for (int pair = FIRST_PAIR; pair <= LAST_PAIR; pair++) {
    auto enabled = time_program_slot(time_program_enabled_id(pair));
    ASSERT_TRUE(enabled.has_value());
    EXPECT_EQ(enabled->pair, pair);
    EXPECT_EQ(enabled->field, TimeProgramField::ENABLED);
    for (int n = FIRST_SWITCH_TIME; n <= LAST_SWITCH_TIME; n++) {
      auto time = time_program_slot(time_program_time_id(pair, n));
      ASSERT_TRUE(time.has_value());
      EXPECT_EQ(time->pair, pair);
      EXPECT_EQ(time->switch_time, n);
      EXPECT_EQ(time->field, TimeProgramField::TIME);
      auto level = time_program_slot(time_program_level_id(pair, n));
      ASSERT_TRUE(level.has_value());
      EXPECT_EQ(level->pair, pair);
      EXPECT_EQ(level->switch_time, n);
      EXPECT_EQ(level->field, TimeProgramField::LEVEL);
    }
  }
}

// The block index of each ID is its place in its pair's block of protocol_ids.h, each index used once.
TEST(TimeProgramIds, EachIdsBlockIndexIsItsPlaceInTheBlock) {
  for (int pair = FIRST_PAIR; pair <= LAST_PAIR; pair++) {
    std::vector<bool> used(TIME_PROGRAM_IDS_PER_PAIR, false);
    for (int id = time_program_enabled_id(pair); id < time_program_enabled_id(pair) + (int) TIME_PROGRAM_IDS_PER_PAIR;
         id++) {
      auto slot = time_program_slot(id);
      ASSERT_TRUE(slot.has_value()) << id;
      const size_t index = time_program_block_index(*slot);
      EXPECT_EQ(index, (size_t) (id - time_program_enabled_id(pair))) << id;
      ASSERT_LT(index, used.size()) << id;
      EXPECT_FALSE(used[index]) << id;
      used[index] = true;
    }
  }
}

TEST(TimeProgramIds, OtherIdsHaveNoSlot) {
  EXPECT_FALSE(time_program_slot(FAN_1_LEVEL_ID).has_value());
  EXPECT_FALSE(time_program_slot(NOT_AN_ID).has_value());
}

TEST(TimeProgramPollList, ListsEachIdOnceInTheOrderAdded) {
  TimeProgramPollList list;
  const int mask = time_program_enabled_id(FIRST_PAIR);
  const int time = time_program_time_id(FIRST_PAIR, FIRST_SWITCH_TIME);

  EXPECT_TRUE(list.add(time));
  EXPECT_TRUE(list.add(mask));
  EXPECT_TRUE(list.add(time));

  EXPECT_EQ(std::vector<int>(list.begin(), list.end()), (std::vector<int>{time, mask}));
}

// A whole block fits; nothing past it does.
TEST(TimeProgramPollList, HoldsOnePairsWholeBlock) {
  TimeProgramPollList list;
  list.add(time_program_enabled_id(FIRST_PAIR));
  for (int n = FIRST_SWITCH_TIME; n <= LAST_SWITCH_TIME; n++) {
    list.add(time_program_time_id(FIRST_PAIR, n));
    list.add(time_program_level_id(FIRST_PAIR, n));
  }
  EXPECT_EQ(list.size(), TIME_PROGRAM_IDS_PER_PAIR);

  EXPECT_FALSE(list.add(time_program_enabled_id(LAST_PAIR)));
  EXPECT_EQ(list.size(), TIME_PROGRAM_IDS_PER_PAIR);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
