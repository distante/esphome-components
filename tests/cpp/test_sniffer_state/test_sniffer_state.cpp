#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "sec_touch/protocol_ids.h"
#include "sec_touch_sniffer/property_store.h"
#include "sec_touch_sniffer/sniffer_state.h"

using esphome::sec_touch::build_sniffer_state;
using esphome::sec_touch::COMMANDID_SET;
using esphome::sec_touch::MAX_PROPERTY_ID;
using esphome::sec_touch::MAX_STATE_LEN;
using esphome::sec_touch::PropertyStore;
using esphome::sec_touch::ScanOutcome;
using esphome::sec_touch::STATE_ELLIPSIS;
using esphome::sec_touch::STATE_MORE_FORMAT;
using esphome::sec_touch::STATE_SEPARATOR;

namespace {

/// An ID an entity has registered, standing in for a fan level.
constexpr int REGISTERED_ID = 4;

void record(PropertyStore &store, int id, const std::string &raw) {
  store.record_message(COMMANDID_SET, id, raw.data(), raw.size(), /*now_s=*/1);
}

std::string state_of(const PropertyStore &store) {
  std::string out;
  build_sniffer_state(store, [](int id) { return id == REGISTERED_ID; }, out);
  return out;
}

}  // namespace

TEST(SnifferState, ListsUnregisteredRecordsWithARawValueInIdOrder) {
  PropertyStore store;
  ASSERT_TRUE(store.allocate(/*with_history=*/false));
  record(store, 5, "0100");
  record(store, 1, "11.11.26");
  record(store, REGISTERED_ID, "2");
  record(store, 3, "SWZ0021E21H");
  store.record_query(7, ScanOutcome::SCAN_OUTCOME_NAK, 1);

  EXPECT_EQ(state_of(store), "1=11.11.26, 3=SWZ0021E21H, 5=0100");
}

TEST(SnifferState, KeepsTheRawValueByteExact) {
  PropertyStore store;
  ASSERT_TRUE(store.allocate(/*with_history=*/false));
  record(store, 9, "00               ");

  EXPECT_EQ(state_of(store), "9=00               ");
}

// IDs 100..255 each list as 20 chars ("100=" + 16), 22 with the separator. 92 entries take 2022 chars; with the
// 13-byte marker ", …+64 more" that is 2035. A 93rd entry would end at 2057, past the cap.
TEST(SnifferState, ACutStateEndsWithTheCountOfEntriesLeftOut) {
  PropertyStore store;
  ASSERT_TRUE(store.allocate(/*with_history=*/false));
  constexpr int FIRST_ID = 100;
  constexpr int LAST_KEPT_ID = 191;
  const std::string value(16, 'v');
  for (int id = FIRST_ID; id <= MAX_PROPERTY_ID; id++) {
    record(store, id, value);
  }

  std::string expected;
  for (int id = FIRST_ID; id <= LAST_KEPT_ID; id++) {
    expected += (id == FIRST_ID ? "" : STATE_SEPARATOR) + std::to_string(id) + "=" + value;
  }
  char more[24];
  std::snprintf(more, sizeof(more), STATE_MORE_FORMAT, static_cast<size_t>(MAX_PROPERTY_ID - LAST_KEPT_ID));
  expected += std::string(STATE_SEPARATOR) + STATE_ELLIPSIS + more;
  const std::string next_entry = STATE_SEPARATOR + std::to_string(LAST_KEPT_ID + 1) + "=" + value;

  EXPECT_EQ(state_of(store), expected);
  EXPECT_LE(expected.size(), MAX_STATE_LEN);
  // The cut is tight: one more entry would not fit.
  EXPECT_GT(expected.size() + next_entry.size(), MAX_STATE_LEN);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
