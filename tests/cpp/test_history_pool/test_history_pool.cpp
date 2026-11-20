#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "../common/history_entries.h"
#include "sec_touch_sniffer/history_pool.h"

using esphome::sec_touch::HistoryPool;
using Entry = sec_touch_test::HistoryEntry;
using sec_touch_test::history_of;

namespace {

void push(HistoryPool &pool, uint8_t id, uint32_t t, const std::string &raw) { pool.push(id, t, raw); }

/// A fast-changing property, like the polled clock.
constexpr uint8_t BUSY_ID = 2;
/// A property that changed only a few times.
constexpr uint8_t QUIET_ID = 9;

}  // namespace

TEST(HistoryPool, ReturnsAnIdsPreviousValuesNewestFirst) {
  HistoryPool pool;
  ASSERT_TRUE(pool.allocate());

  push(pool, QUIET_ID, 10, "0000");
  push(pool, BUSY_ID, 11, "11:00");
  push(pool, QUIET_ID, 20, "0100");

  EXPECT_EQ(history_of(pool, QUIET_ID), (std::vector<Entry>{{20, "0100"}, {10, "0000"}}));
}

// The depth per ID counts the current value, which lives in the record, so the pool keeps one fewer previous value.
TEST(HistoryPool, OnePreviousValueTooManyDropsThatIdsOldest) {
  HistoryPool pool;
  ASSERT_TRUE(pool.allocate());
  push(pool, QUIET_ID, 1, "q");
  // One more than the pool keeps for an ID: first seen at 1..MAX_PREVIOUS_PER_ID + 1.
  for (uint32_t t = 1; t <= HistoryPool::MAX_PREVIOUS_PER_ID + 1u; t++) {
    push(pool, BUSY_ID, t, std::to_string(t));
  }

  std::vector<Entry> kept;
  for (uint32_t t = HistoryPool::MAX_PREVIOUS_PER_ID + 1u; t >= 2; t--) {
    kept.emplace_back(t, std::to_string(t));
  }
  EXPECT_EQ(history_of(pool, BUSY_ID), kept);
  EXPECT_EQ(history_of(pool, QUIET_ID), (std::vector<Entry>{{1, "q"}}));
}

// When the pool is full, a fast-changing ID can't push out the history of rare IDs.
TEST(HistoryPool, AFullPoolDropsTheOldestEntryOfTheIdWithTheMostEntries) {
  HistoryPool pool;
  ASSERT_TRUE(pool.allocate());
  push(pool, QUIET_ID, 1, "quiet-old");
  push(pool, QUIET_ID, 2, "quiet-new");
  // Fill the pool with many IDs holding one 40-byte value each, plus a busier ID holding three.
  const std::string filler(40, 'x');
  uint8_t id = 20;
  for (uint32_t t = 3; t <= 5; t++) {
    push(pool, BUSY_ID, t, filler);
  }
  while (pool.free_bytes() >= filler.size() + HistoryPool::ENTRY_HEADER_BYTES) {
    push(pool, id++, 6, filler);
  }

  push(pool, id, 7, filler);

  EXPECT_EQ(history_of(pool, BUSY_ID), (std::vector<Entry>{{5, filler}, {4, filler}}));
  EXPECT_EQ(history_of(pool, QUIET_ID), (std::vector<Entry>{{2, "quiet-new"}, {1, "quiet-old"}}));
  EXPECT_EQ(history_of(pool, id), (std::vector<Entry>{{7, filler}}));
}

TEST(HistoryPool, AnUnallocatedPoolKeepsNothing) {
  HistoryPool pool;

  push(pool, QUIET_ID, 1, "0000");

  EXPECT_TRUE(history_of(pool, QUIET_ID).empty());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
