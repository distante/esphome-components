#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "../common/history_entries.h"
#include "sec_touch/protocol_ids.h"
#include "sec_touch_sniffer/property_store.h"

using esphome::sec_touch::COMMANDID_SET;
using esphome::sec_touch::MAX_DATA_VALUE_LEN;
using esphome::sec_touch::MAX_PROPERTY_ID;
using esphome::sec_touch::PropertyStore;
using esphome::sec_touch::RAW_VALUE_CAP;
using esphome::sec_touch::RecordResult;
using esphome::sec_touch::ScanOutcome;

using Entry = sec_touch_test::HistoryEntry;

namespace {

constexpr int PROPERTY_ID = 5;
/// A command id no message has carried so far.
constexpr int OTHER_COMMAND_ID = 33;

PropertyStore allocated_store(bool with_history = true) {
  PropertyStore store;
  EXPECT_TRUE(store.allocate(with_history));
  return store;
}

RecordResult record(PropertyStore &store, int id, const std::string &raw, uint32_t now_s,
                    int command_id = COMMANDID_SET) {
  return store.record_message(command_id, id, raw, now_s);
}

std::string raw_of(const PropertyStore &store, int id) { return std::string(store.raw_value(id)); }

std::vector<Entry> history_of(const PropertyStore &store, uint8_t id) {
  return sec_touch_test::history_of(store.history(), id);
}

}  // namespace

TEST(PropertyStore, TheFirstSightingIsNotAChange) {
  PropertyStore store = allocated_store();

  EXPECT_EQ(record(store, PROPERTY_ID, "00               ", 7), RecordResult::RECORD_NEW);

  const auto &f = store.fields(PROPERTY_ID);
  EXPECT_TRUE(store.has_raw_value(PROPERTY_ID));
  EXPECT_EQ(raw_of(store, PROPERTY_ID), "00               ");
  EXPECT_EQ(f.command_id, COMMANDID_SET);
  EXPECT_EQ(f.first_seen_s, 7u);
  EXPECT_EQ(f.last_seen_s, 7u);
  EXPECT_EQ(f.last_changed_s, 7u);
  EXPECT_EQ(f.change_count, 0u);
}

TEST(PropertyStore, ARepeatedIdenticalMessageOnlyUpdatesLastSeen) {
  PropertyStore store = allocated_store();
  record(store, PROPERTY_ID, "0100", 7);
  const uint32_t seq_before = store.seq();

  EXPECT_EQ(record(store, PROPERTY_ID, "0100", 9), RecordResult::RECORD_UNCHANGED);

  const auto &f = store.fields(PROPERTY_ID);
  EXPECT_EQ(f.last_seen_s, 9u);
  EXPECT_EQ(f.last_changed_s, 7u);
  EXPECT_EQ(f.change_count, 0u);
  EXPECT_GT(store.seq(), seq_before);
  EXPECT_EQ(f.seq, store.seq());
  EXPECT_TRUE(history_of(store, PROPERTY_ID).empty());
}

TEST(PropertyStore, DifferentBytesAreAChangeAndThePreviousValueEntersTheHistory) {
  PropertyStore store = allocated_store();
  record(store, PROPERTY_ID, "0000", 3);
  const uint32_t seq_before = store.seq();

  EXPECT_EQ(record(store, PROPERTY_ID, "0100", 8), RecordResult::RECORD_CHANGED);

  const auto &f = store.fields(PROPERTY_ID);
  EXPECT_EQ(raw_of(store, PROPERTY_ID), "0100");
  EXPECT_EQ(f.change_count, 1u);
  EXPECT_EQ(f.last_changed_s, 8u);
  EXPECT_EQ(f.last_seen_s, 8u);
  EXPECT_EQ(f.first_seen_s, 3u);
  EXPECT_GT(store.seq(), seq_before);
  EXPECT_EQ(history_of(store, PROPERTY_ID), (std::vector<Entry>{{3, "0000"}}));
}

TEST(PropertyStore, ADifferentCommandIdAloneIsNotAChange) {
  PropertyStore store = allocated_store();
  record(store, PROPERTY_ID, "0100", 3);

  EXPECT_EQ(record(store, PROPERTY_ID, "0100", 4, OTHER_COMMAND_ID), RecordResult::RECORD_UNCHANGED);

  EXPECT_EQ(store.fields(PROPERTY_ID).command_id, OTHER_COMMAND_ID);
  EXPECT_EQ(store.fields(PROPERTY_ID).change_count, 0u);
}

TEST(PropertyStore, PassiveMessagesLeaveAPropertyUnqueried) {
  PropertyStore store = allocated_store();

  record(store, PROPERTY_ID, "0100", 3);

  EXPECT_FALSE(store.is_queried(PROPERTY_ID));
}

TEST(PropertyStore, AScanQueryOutcomeSetsQueryStatusAndBumpsSeq) {
  PropertyStore store = allocated_store();
  record(store, PROPERTY_ID, "0100", 3);
  const uint32_t seq_before = store.seq();

  store.record_query(PROPERTY_ID, ScanOutcome::SCAN_OUTCOME_OK, 4);

  EXPECT_TRUE(store.is_queried(PROPERTY_ID));
  EXPECT_EQ(store.fields(PROPERTY_ID).query_status, ScanOutcome::SCAN_OUTCOME_OK);
  EXPECT_EQ(store.fields(PROPERTY_ID).query_status_at_s, 4u);
  EXPECT_GT(store.seq(), seq_before);
}

TEST(PropertyStore, AFailureKeepsTheLastGoodValueHistoryAndChangeCount) {
  PropertyStore store = allocated_store();
  record(store, PROPERTY_ID, "0000", 1);
  record(store, PROPERTY_ID, "0100", 2);

  store.record_query(PROPERTY_ID, ScanOutcome::SCAN_OUTCOME_TIMEOUT, 9);

  const auto &f = store.fields(PROPERTY_ID);
  EXPECT_EQ(f.query_status, ScanOutcome::SCAN_OUTCOME_TIMEOUT);
  EXPECT_EQ(f.query_status_at_s, 9u);
  EXPECT_EQ(raw_of(store, PROPERTY_ID), "0100");
  EXPECT_EQ(f.change_count, 1u);
  EXPECT_EQ(f.last_seen_s, 2u);
  EXPECT_EQ(history_of(store, PROPERTY_ID), (std::vector<Entry>{{1, "0000"}}));
}

TEST(PropertyStore, ARecordThatOnlyEverFailedHasNoRawValue) {
  PropertyStore store = allocated_store();

  store.record_query(PROPERTY_ID, ScanOutcome::SCAN_OUTCOME_NAK, 4);

  EXPECT_FALSE(store.has_raw_value(PROPERTY_ID));
  EXPECT_TRUE(store.is_queried(PROPERTY_ID));
  EXPECT_EQ(store.fields(PROPERTY_ID).query_status, ScanOutcome::SCAN_OUTCOME_NAK);
  EXPECT_TRUE(history_of(store, PROPERTY_ID).empty());
}

TEST(PropertyStore, AnIdAboveTheSlotsIsDroppedAndCounted) {
  PropertyStore store = allocated_store();
  const uint32_t seq_before = store.seq();

  EXPECT_EQ(record(store, MAX_PROPERTY_ID + 1, "1", 3), RecordResult::RECORD_DROPPED);
  EXPECT_EQ(record(store, MAX_PROPERTY_ID, "1", 3), RecordResult::RECORD_NEW);

  EXPECT_EQ(store.dropped(), 1u);
  EXPECT_EQ(store.seq(), seq_before + 1);
}

TEST(PropertyStore, WithoutHistoryAChangeStillUpdatesTheRecord) {
  PropertyStore store = allocated_store(/*with_history=*/false);
  record(store, PROPERTY_ID, "0000", 1);

  EXPECT_EQ(record(store, PROPERTY_ID, "0100", 2), RecordResult::RECORD_CHANGED);

  EXPECT_EQ(raw_of(store, PROPERTY_ID), "0100");
  EXPECT_EQ(store.fields(PROPERTY_ID).change_count, 1u);
  EXPECT_TRUE(history_of(store, PROPERTY_ID).empty());
}

TEST(PropertyStore, TheLongestValueAFrameCanCarryIsKeptWhole) {
  PropertyStore store = allocated_store();
  const std::string longest(MAX_DATA_VALUE_LEN, 'v');

  EXPECT_EQ(record(store, PROPERTY_ID, longest, 1), RecordResult::RECORD_NEW);

  EXPECT_EQ(raw_of(store, PROPERTY_ID), longest);
}

// `dropped` counts only IDs without a slot; a value too long to store is not truncated and not counted there.
TEST(PropertyStore, AValueLongerThanTheCapIsRejectedNotDropped) {
  PropertyStore store = allocated_store();
  const std::string too_long(RAW_VALUE_CAP + 1, 'v');

  EXPECT_EQ(record(store, PROPERTY_ID, too_long, 1), RecordResult::RECORD_REJECTED);

  EXPECT_FALSE(store.has_raw_value(PROPERTY_ID));
  EXPECT_EQ(store.dropped(), 0u);
}

TEST(PropertyStore, AClearForgetsEveryRecordItsHistoryAndTheDroppedCount) {
  PropertyStore store = allocated_store();
  record(store, PROPERTY_ID, "0000", 1);
  record(store, PROPERTY_ID, "0100", 2);
  store.record_query(MAX_PROPERTY_ID, ScanOutcome::SCAN_OUTCOME_NAK, 3);
  record(store, MAX_PROPERTY_ID + 1, "1", 3);

  store.clear();

  EXPECT_FALSE(store.has_raw_value(PROPERTY_ID));
  EXPECT_FALSE(store.is_queried(MAX_PROPERTY_ID));
  EXPECT_EQ(store.fields(PROPERTY_ID).change_count, 0u);
  EXPECT_EQ(store.fields(PROPERTY_ID).seq, 0u);
  EXPECT_EQ(store.fields(MAX_PROPERTY_ID).seq, 0u);
  EXPECT_TRUE(history_of(store, PROPERTY_ID).empty());
  EXPECT_EQ(store.dropped(), 0u);
}

// A page polling with an old `since` must never see `seq` go down.
TEST(PropertyStore, AfterAClearSeqKeepsGoingUpAndAMessageIsNewAgain) {
  PropertyStore store = allocated_store();
  record(store, PROPERTY_ID, "0100", 1);
  const uint32_t seq_before = store.seq();

  store.clear();

  EXPECT_EQ(store.seq(), seq_before);
  EXPECT_EQ(record(store, PROPERTY_ID, "0100", 9), RecordResult::RECORD_NEW);
  EXPECT_EQ(store.fields(PROPERTY_ID).first_seen_s, 9u);
  EXPECT_GT(store.seq(), seq_before);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
