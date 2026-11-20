#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "sec_touch_sniffer/history_pool.h"

using esphome::sec_touch::HistoryPool;

namespace {

using Entry = std::pair<uint32_t, std::string>;

/// The ID's previous values as `{first seen, raw value}`, newest first.
std::vector<Entry> history_of(const HistoryPool &pool, uint8_t id) {
  std::vector<Entry> entries;
  pool.for_each_newest_first(
      id, [&](uint32_t t, const char *bytes, uint8_t len) { entries.emplace_back(t, std::string(bytes, len)); });
  return entries;
}

void push(HistoryPool &pool, uint8_t id, uint32_t t, const std::string &raw) {
  pool.push(id, t, raw.data(), static_cast<uint8_t>(raw.size()));
}

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

// Depth 8 per ID counts the current value, which lives in the record, so the pool keeps 7 previous values.
TEST(HistoryPool, AnEighthPreviousValueDropsThatIdsOldest) {
  HistoryPool pool;
  ASSERT_TRUE(pool.allocate());
  push(pool, BUSY_ID, 1, "a");
  push(pool, QUIET_ID, 2, "q");
  for (uint32_t t = 3; t <= 9; t++) {
    push(pool, BUSY_ID, t, std::to_string(t));
  }

  EXPECT_EQ(history_of(pool, BUSY_ID),
            (std::vector<Entry>{{9, "9"}, {8, "8"}, {7, "7"}, {6, "6"}, {5, "5"}, {4, "4"}, {3, "3"}}));
  EXPECT_EQ(history_of(pool, QUIET_ID), (std::vector<Entry>{{2, "q"}}));
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
