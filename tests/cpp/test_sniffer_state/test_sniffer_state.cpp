#include <gtest/gtest.h>

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
  const std::string value(16, 'v');
  for (int id = 100; id <= MAX_PROPERTY_ID; id++) {
    record(store, id, value);
  }

  const std::string state = state_of(store);

  EXPECT_LE(state.size(), MAX_STATE_LEN);
  EXPECT_EQ(state.size(), 2035u);
  const std::string tail = "191=vvvvvvvvvvvvvvvv, \xE2\x80\xA6+64 more";
  EXPECT_EQ(state.substr(state.size() - tail.size()), tail);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
