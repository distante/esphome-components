#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "sec_touch/protocol_ids.h"
#include "sec_touch_sniffer/history_pool.h"
#include "sec_touch_sniffer/json_chunk_writer.h"
#include "sec_touch_sniffer/property_store.h"
#include "sec_touch_sniffer/sniffer_data.h"
#include "sec_touch_sniffer/write_probe.h"

using esphome::sec_touch::CHECK_RUN;
using esphome::sec_touch::COMMANDID_SET;
using esphome::sec_touch::DATE_PROPERTY_ID;
using esphome::sec_touch::DEFAULT_DISCOVER_RUN;
using esphome::sec_touch::DELTA_END;
using esphome::sec_touch::DeviceInfo;
using esphome::sec_touch::FAN_LABEL_IDS;
using esphome::sec_touch::FAN_LEVEL_IDS;
using esphome::sec_touch::SUMMER_VENTILATION_PROPERTY_ID;
using esphome::sec_touch::first_scan_id;
using esphome::sec_touch::HistoryPool;
using esphome::sec_touch::JSON_CHUNK_BYTES;
using esphome::sec_touch::JsonChunkWriter;
using esphome::sec_touch::MAX_PROPERTY_ID;
using esphome::sec_touch::next_record_in_delta;
using esphome::sec_touch::NO_SCAN_ID;
using esphome::sec_touch::PropertyStore;
using esphome::sec_touch::RecordSnapshot;
using esphome::sec_touch::scan_start_reply;
using esphome::sec_touch::scan_stop_reply;
using esphome::sec_touch::scan_state;
using esphome::sec_touch::ScanFields;
using esphome::sec_touch::ScanGoal;
using esphome::sec_touch::ScanOutcome;
using esphome::sec_touch::ScanRun;
using esphome::sec_touch::ScanState;
using esphome::sec_touch::since_from_uri;
using esphome::sec_touch::sniffer_route;
using esphome::sec_touch::SNIFFER_CLEAR_PATH;
using esphome::sec_touch::SNIFFER_DATA_PATH;
using esphome::sec_touch::SNIFFER_PAGE_PATH;
using esphome::sec_touch::SNIFFER_SCAN_START_PATH;
using esphome::sec_touch::SNIFFER_SCAN_STOP_PATH;
using esphome::sec_touch::SnifferMethod;
using esphome::sec_touch::SnifferRoute;
using esphome::sec_touch::snapshot_record;
using esphome::sec_touch::probe_start_result;
using esphome::sec_touch::ProbeKind;
using esphome::sec_touch::ProbeRestore;
using esphome::sec_touch::ProbeResult;
using esphome::sec_touch::ProbeStep;
using esphome::sec_touch::ProbeStepKind;
using esphome::sec_touch::ProbeValue;
using esphome::sec_touch::ProbeVerdict;
using esphome::sec_touch::SNIFFER_PROBE_PATH;
using esphome::sec_touch::SNIFFER_RESTORE_PATH;
using esphome::sec_touch::write_busy_json;
using esphome::sec_touch::write_data_json;
using esphome::sec_touch::write_probe_json;
using esphome::sec_touch::write_record_json;
using esphome::sec_touch::write_scan_json;

namespace {

constexpr int PROPERTY_ID = 5;

PropertyStore allocated_store(bool with_history = true) {
  PropertyStore store;
  EXPECT_TRUE(store.allocate(with_history));
  return store;
}

void record(PropertyStore &store, int id, const std::string &raw, uint32_t now_s) {
  store.record_message(COMMANDID_SET, id, raw, now_s);
}

/// The spec's example record: "0000" at 1 s, "0100" at 3 s, a scan `ok` at 4 s, "0100" again at 5 s.
void record_spec_example(PropertyStore &store, int id = PROPERTY_ID) {
  record(store, id, "0000", 1);
  record(store, id, "0100", 3);
  store.record_query(id, ScanOutcome::SCAN_OUTCOME_OK, 4);
  record(store, id, "0100", 5);
}

bool collect(void *context, const char *data, size_t len) {
  static_cast<std::string *>(context)->append(data, len);
  return true;
}

/// Writes with `write`, flushes, and returns the whole output.
template<typename F> std::string written(F write) {
  std::string out;
  JsonChunkWriter w(&collect, &out);
  write(w);
  w.flush();
  return out;
}

std::string record_json(const PropertyStore &store, int id, bool registered = false) {
  static RecordSnapshot snapshot;
  snapshot_record(store, id, snapshot);
  return written([&](JsonChunkWriter &w) { write_record_json(w, snapshot, registered); });
}

std::vector<int> delta_ids(const PropertyStore &store, uint32_t since) {
  std::vector<int> ids;
  for (int id = next_record_in_delta(store, 0, since); id != DELTA_END;
       id = next_record_in_delta(store, id + 1, since)) {
    ids.push_back(id);
  }
  return ids;
}

const DeviceInfo DEVICE{"aabbccddeeff", "esp32-power-meter", "9f2c41ab", 812000};
/// The scan before any run.
const ScanState NO_RUN = scan_state(ScanFields{});

/// A with_lock() that records whether the lock is held, so a sink can check it.
struct FakeLock {
  bool held = false;
  int taken = 0;
  auto with_lock() {
    return [this](auto f) {
      this->held = true;
      this->taken++;
      f();
      this->held = false;
    };
  }
};

std::string data_json(const PropertyStore &store, uint32_t since, const ScanState &scan = NO_RUN,
                      const ProbeResult *probe = nullptr) {
  static RecordSnapshot scratch;
  FakeLock lock;
  std::string out;
  JsonChunkWriter w(&collect, &out);
  EXPECT_TRUE(
      write_data_json(w, store, DEVICE, scan, probe, since, scratch, lock.with_lock(), [](int) { return false; }));
  return out;
}

}  // namespace

// --- Record JSON ---

TEST(SnifferData, ARecordMatchesTheSpecExample) {
  PropertyStore store = allocated_store();
  record_spec_example(store);

  EXPECT_EQ(record_json(store, PROPERTY_ID),
            R"({"id":5,"cmd":32,"raw":"0100","first":1000,"last":5000,"changed":3000,"changes":1,)"
            R"("status":"ok","status_at":4000,"reg":false,"hist":[[3000,"0100"],[1000,"0000"]]})");
}

TEST(SnifferData, ARegisteredRecordSaysSo) {
  PropertyStore store = allocated_store();
  record(store, PROPERTY_ID, "1", 2);

  EXPECT_EQ(record_json(store, PROPERTY_ID, /*registered=*/true),
            R"({"id":5,"cmd":32,"raw":"1","first":2000,"last":2000,"changed":2000,"changes":0,)"
            R"("status":"unqueried","status_at":null,"reg":true,"hist":[[2000,"1"]]})");
}

TEST(SnifferData, ARecordThatOnlyFailedHasNullRawAndEmptyHistory) {
  PropertyStore store = allocated_store();
  store.record_query(PROPERTY_ID, ScanOutcome::SCAN_OUTCOME_NAK, 4);

  EXPECT_EQ(record_json(store, PROPERTY_ID),
            R"({"id":5,"cmd":null,"raw":null,"first":null,"last":null,"changed":null,"changes":0,)"
            R"("status":"nak","status_at":4000,"reg":false,"hist":[]})");
}

TEST(SnifferData, ATimedOutQueryKeepsTheLastGoodValue) {
  PropertyStore store = allocated_store();
  record(store, PROPERTY_ID, "7", 1);
  store.record_query(PROPERTY_ID, ScanOutcome::SCAN_OUTCOME_TIMEOUT, 9);

  EXPECT_EQ(record_json(store, PROPERTY_ID),
            R"({"id":5,"cmd":32,"raw":"7","first":1000,"last":1000,"changed":1000,"changes":0,)"
            R"("status":"timeout","status_at":9000,"reg":false,"hist":[[1000,"7"]]})");
}

TEST(SnifferData, WithoutTheHistoryPoolTheHistoryIsEmpty) {
  PropertyStore store = allocated_store(/*with_history=*/false);
  record(store, PROPERTY_ID, "0000", 1);
  record(store, PROPERTY_ID, "0100", 3);

  EXPECT_NE(record_json(store, PROPERTY_ID).find(R"("hist":[])"), std::string::npos);
}

TEST(SnifferData, TheHistoryHoldsEveryKeptValueNewestFirst) {
  PropertyStore store = allocated_store();
  // One more change than the pool keeps, so the oldest value is gone.
  for (uint32_t t = 1; t <= HistoryPool::MAX_PREVIOUS_PER_ID + 2u; t++) {
    record(store, PROPERTY_ID, std::to_string(t), t);
  }

  EXPECT_NE(record_json(store, PROPERTY_ID)
                .find(R"("hist":[[9000,"9"],[8000,"8"],[7000,"7"],[6000,"6"],[5000,"5"],[4000,"4"],[3000,"3"],)"
                      R"([2000,"2"]]})"),
            std::string::npos);
}

TEST(SnifferData, RawBytesAreEscapedInTheValueAndTheHistory) {
  PropertyStore store = allocated_store();
  record(store, PROPERTY_ID, "a\x01", 1);
  record(store, PROPERTY_ID, "\xFF\"", 2);

  EXPECT_NE(record_json(store, PROPERTY_ID).find(R"("raw":"\u00ff\"")"), std::string::npos);
  EXPECT_NE(record_json(store, PROPERTY_ID).find(R"("hist":[[2000,"\u00ff\""],[1000,"a\u0001"]])"), std::string::npos);
}

TEST(SnifferData, TheSnapshotStaysWhenTheStoreChangesAfterIt) {
  PropertyStore store = allocated_store();
  record(store, PROPERTY_ID, "old", 1);
  static RecordSnapshot snapshot;
  snapshot_record(store, PROPERTY_ID, snapshot);

  record(store, PROPERTY_ID, "new", 2);

  EXPECT_EQ(written([&](JsonChunkWriter &w) { write_record_json(w, snapshot, false); }),
            R"({"id":5,"cmd":32,"raw":"old","first":1000,"last":1000,"changed":1000,"changes":0,)"
            R"("status":"unqueried","status_at":null,"reg":false,"hist":[[1000,"old"]]})");
}

// --- Delta selection by seq ---

TEST(SnifferData, SinceZeroSelectsEveryRecordWithAMessageOrQuery) {
  PropertyStore store = allocated_store();
  record(store, 7, "a", 1);
  store.record_query(3, ScanOutcome::SCAN_OUTCOME_NAK, 2);
  record(store, MAX_PROPERTY_ID, "z", 3);

  EXPECT_EQ(delta_ids(store, 0), (std::vector<int>{3, 7, MAX_PROPERTY_ID}));
}

TEST(SnifferData, AnEmptyStoreSelectsNothing) {
  PropertyStore store = allocated_store();
  EXPECT_EQ(delta_ids(store, 0), std::vector<int>{});
}

TEST(SnifferData, AnUnallocatedStoreSelectsNothing) {
  PropertyStore store;
  EXPECT_EQ(delta_ids(store, 0), std::vector<int>{});
}

TEST(SnifferData, ADeltaSelectsOnlyRecordsChangedSinceS) {
  PropertyStore store = allocated_store();
  record(store, 1, "a", 1);
  record(store, 2, "b", 1);
  const uint32_t since = store.seq();
  record(store, 9, "c", 2);
  record(store, 1, "a", 3);  // unchanged raw value: last_seen still moves seq

  EXPECT_EQ(delta_ids(store, since), (std::vector<int>{1, 9}));
}

TEST(SnifferData, AScanQueryPutsTheRecordInTheDelta) {
  PropertyStore store = allocated_store();
  record(store, 1, "a", 1);
  const uint32_t since = store.seq();
  store.record_query(1, ScanOutcome::SCAN_OUTCOME_TIMEOUT, 2);

  EXPECT_EQ(delta_ids(store, since), std::vector<int>{1});
}

TEST(SnifferData, ADeltaAtTheCurrentSeqSelectsNothing) {
  PropertyStore store = allocated_store();
  record(store, 1, "a", 1);
  EXPECT_EQ(delta_ids(store, store.seq()), std::vector<int>{});
}

// --- Scan ---

namespace {

/// The spec example's discover range. 42 and 208 are unknown properties, so a discover of it queries both.
constexpr ScanRun DISCOVER_1_208{ScanGoal::SCAN_GOAL_DISCOVER, 1, 208};
constexpr int MID_RANGE_ID = 42;

ScanFields running(const ScanRun &run, int current) {
  return ScanFields{.ran = true, .active = true, .completed = false, .run = run, .current = current};
}

ScanFields stopped(const ScanRun &run, int current) {
  return ScanFields{.ran = true, .active = false, .completed = false, .run = run, .current = current};
}

ScanFields completed(const ScanRun &run) {
  return ScanFields{.ran = true, .active = false, .completed = true, .run = run, .current = NO_SCAN_ID};
}

/// A discover list run of `ids`.
ScanRun listed(const std::vector<int> &ids) {
  ScanRun run = DEFAULT_DISCOVER_RUN;
  run.listed = true;
  for (int id : ids) {
    run.ids.add(id);
  }
  return run;
}

std::string scan_json(const ScanState &scan) {
  return written([&](JsonChunkWriter &w) { write_scan_json(w, scan); });
}

std::string check_json(bool active, const std::string &next, bool done) {
  return std::string(R"({"active":)") + (active ? "true" : "false") +
         R"(,"goal":"check","from":null,"to":null,"ids":null,)" + R"("next":)" + next + R"(,"done":)" +
         (done ? "true" : "false") + "}";
}

/// DISCOVER_1_208 just started: running at its first ID.
std::string discover_1_208_start_json() {
  return R"({"active":true,"goal":"discover","from":1,"to":208,"ids":null,"next":)" +
         std::to_string(first_scan_id(DISCOVER_1_208)) + R"(,"done":false})";
}

}  // namespace

TEST(SnifferData, BeforeAnyRunTheScanIsAnIdleDiscoverWithNoRangeOrPosition) {
  EXPECT_EQ(scan_json(NO_RUN),
            R"({"active":false,"goal":"discover","from":null,"to":null,"ids":null,"next":null,"done":false})");
}

TEST(SnifferData, ARunningDiscoverShowsItsRangeAndNextId) {
  EXPECT_EQ(scan_json(scan_state(running(DISCOVER_1_208, MID_RANGE_ID))),
            R"({"active":true,"goal":"discover","from":1,"to":208,"ids":null,"next":42,"done":false})");
}

TEST(SnifferData, WhileTheLastIdIsQueriedTheScanShowsItAndIsNotDone) {
  EXPECT_EQ(scan_json(scan_state(running(DISCOVER_1_208, NO_SCAN_ID))),
            R"({"active":true,"goal":"discover","from":1,"to":208,"ids":null,"next":208,"done":false})");
}

TEST(SnifferData, AStoppedScanShowsTheIdItStoppedAt) {
  EXPECT_EQ(scan_json(scan_state(stopped(DISCOVER_1_208, MID_RANGE_ID))),
            R"({"active":false,"goal":"discover","from":1,"to":208,"ids":null,"next":42,"done":false})");
}

// Stopped with the last query in flight: its outcome was never recorded, so the run did not cover all its IDs.
TEST(SnifferData, ARunStoppedWithTheLastQueryInFlightIsNotDone) {
  EXPECT_EQ(scan_json(scan_state(stopped(DISCOVER_1_208, NO_SCAN_ID))),
            R"({"active":false,"goal":"discover","from":1,"to":208,"ids":null,"next":208,"done":false})");
}

TEST(SnifferData, ACompletedScanIsDoneWithNoNextId) {
  EXPECT_EQ(scan_json(scan_state(completed(DISCOVER_1_208))),
            R"({"active":false,"goal":"discover","from":1,"to":208,"ids":null,"next":null,"done":true})");
}

TEST(SnifferData, AScanStartedAgainAfterACompletedRunIsNotDone) {
  ScanFields fields = completed(DISCOVER_1_208);
  fields.active = true;
  fields.current = first_scan_id(DISCOVER_1_208);
  EXPECT_FALSE(scan_state(fields).done);
}

TEST(SnifferData, ACheckHasNoRange) {
  EXPECT_EQ(scan_json(scan_state(running(CHECK_RUN, FAN_LABEL_IDS.front()))),
            check_json(true, std::to_string(FAN_LABEL_IDS.front()), false));
  EXPECT_EQ(scan_json(scan_state(running(CHECK_RUN, NO_SCAN_ID))),
            check_json(true, std::to_string(FAN_LEVEL_IDS.back()), false));
  EXPECT_EQ(scan_json(scan_state(stopped(CHECK_RUN, FAN_LABEL_IDS.front()))),
            check_json(false, std::to_string(FAN_LABEL_IDS.front()), false));
  EXPECT_EQ(scan_json(scan_state(completed(CHECK_RUN))), check_json(false, "null", true));
}

// --- Scan start and stop replies ---

// No resume: an accepted start begins at the goal's first ID, whatever ran before. A busy start never gets this reply:
// it is a 409 with the current scan.
TEST(SnifferData, AStartReplyBeginsTheRequestedRunAtItsFirstId) {
  EXPECT_EQ(scan_json(scan_start_reply(DISCOVER_1_208)), discover_1_208_start_json());
  EXPECT_EQ(scan_json(scan_start_reply(CHECK_RUN)), check_json(true, std::to_string(DATE_PROPERTY_ID), false));
  const ScanRun requested{ScanGoal::SCAN_GOAL_DISCOVER, 5, 208};
  EXPECT_EQ(scan_json(scan_start_reply(requested)),
            R"({"active":true,"goal":"discover","from":5,"to":208,"ids":null,"next":5,"done":false})");
}

TEST(SnifferData, AStopReplyMidScanShowsWhereItStopped) {
  EXPECT_EQ(scan_json(scan_stop_reply(running(DISCOVER_1_208, MID_RANGE_ID))),
            R"({"active":false,"goal":"discover","from":1,"to":208,"ids":null,"next":42,"done":false})");
}

TEST(SnifferData, AStopReplyWhileIdleIsTheIdleScan) {
  EXPECT_EQ(scan_json(scan_stop_reply(ScanFields{})), scan_json(NO_RUN));
  EXPECT_EQ(scan_json(scan_stop_reply(completed(CHECK_RUN))), check_json(false, "null", true));
}

// --- Narrowed scans: list runs ---

TEST(SnifferData, AListRunShowsItsIdsAscendingAndNoRange) {
  EXPECT_EQ(scan_json(scan_state(running(listed({77, 12, 40}), 40))),
            R"({"active":true,"goal":"discover","from":null,"to":null,"ids":[12,40,77],"next":40,"done":false})");
}

TEST(SnifferData, ACompletedListRunKeepsItsIds) {
  EXPECT_EQ(scan_json(scan_state(completed(listed({12, 40})))),
            R"({"active":false,"goal":"discover","from":null,"to":null,"ids":[12,40],"next":null,"done":true})");
}

// The list as requested: a known ID in it is skipped by the run but still reported.
TEST(SnifferData, AListRunReportsAKnownIdItSkips) {
  const ScanRun run = listed({SUMMER_VENTILATION_PROPERTY_ID, SUMMER_VENTILATION_PROPERTY_ID + 1});
  EXPECT_EQ(scan_json(scan_start_reply(run)), R"({"active":true,"goal":"discover","from":null,"to":null,"ids":[)" +
                                                  std::to_string(SUMMER_VENTILATION_PROPERTY_ID) + "," +
                                                  std::to_string(SUMMER_VENTILATION_PROPERTY_ID + 1) + R"(],"next":)" +
                                                  std::to_string(SUMMER_VENTILATION_PROPERTY_ID + 1) +
                                                  R"(,"done":false})");
}

TEST(SnifferData, AStartReplyOfAListRunBeginsAtItsLowestId) {
  EXPECT_EQ(scan_json(scan_start_reply(listed({77, 12}))),
            R"({"active":true,"goal":"discover","from":null,"to":null,"ids":[12,77],"next":12,"done":false})");
}

// --- Data response ---

TEST(SnifferData, SinceZeroIsAFullResponse) {
  PropertyStore store = allocated_store();
  record_spec_example(store);

  EXPECT_EQ(
      data_json(store, 0, scan_state(running(DISCOVER_1_208, MID_RANGE_ID))),
      R"({"mac":"aabbccddeeff","name":"esp32-power-meter","boot":"9f2c41ab","uptime":812000,)"
      R"("seq":4,"full":true,"dropped":0,"scan":{"active":true,"goal":"discover","from":1,"to":208,"ids":null,"next":42,"done":false},"probe":null,)"
      R"("records":[{"id":5,"cmd":32,"raw":"0100","first":1000,"last":5000,"changed":3000,"changes":1,)"
      R"("status":"ok","status_at":4000,"reg":false,"hist":[[3000,"0100"],[1000,"0000"]]}]})");
}

TEST(SnifferData, ADeltaIsNotFullAndHoldsOnlyChangedRecords) {
  PropertyStore store = allocated_store();
  record(store, 1, "a", 1);
  const uint32_t since = store.seq();
  record(store, 2, "b", 2);
  record(store, 3, "c", 3);
  store.record_message(COMMANDID_SET, MAX_PROPERTY_ID + 1, "x", 3);

  EXPECT_EQ(
      data_json(store, since),
      R"({"mac":"aabbccddeeff","name":"esp32-power-meter","boot":"9f2c41ab","uptime":812000,)"
      R"("seq":3,"full":false,"dropped":1,"scan":{"active":false,"goal":"discover","from":null,"to":null,"ids":null,"next":null,"done":false},"probe":null,"records":[)"
      R"({"id":2,"cmd":32,"raw":"b","first":2000,"last":2000,"changed":2000,"changes":0,)"
      R"("status":"unqueried","status_at":null,"reg":false,"hist":[[2000,"b"]]},)"
      R"({"id":3,"cmd":32,"raw":"c","first":3000,"last":3000,"changed":3000,"changes":0,)"
      R"("status":"unqueried","status_at":null,"reg":false,"hist":[[3000,"c"]]}]})");
}

// --- Probe ---

namespace {

constexpr uint8_t PROBE_ID = 77;

ProbeStep step(ProbeStepKind kind, const std::string &value, uint64_t at_ms) {
  return ProbeStep{kind, ScanOutcome::SCAN_OUTCOME_OK, true, ProbeValue::of(value), at_ms};
}

/// The spec's example: 77 probed with "03" over "01", writable and restored.
ProbeResult spec_example_probe() {
  ProbeResult probe = probe_start_result(ProbeKind::PROBE_KIND_PROBE, PROBE_ID, ProbeValue::of("03"));
  probe.active = false;
  probe.has_original = true;
  probe.original = ProbeValue::of("01");
  probe.steps[0] = step(ProbeStepKind::PROBE_STEP_GET, "01", 812000);
  probe.steps[1] = step(ProbeStepKind::PROBE_STEP_SET, "03", 812400);
  probe.step_count = 2;
  probe.verdict = ProbeVerdict::PROBE_VERDICT_WRITABLE;
  probe.restore = ProbeRestore::PROBE_RESTORE_OK;
  return probe;
}

std::string probe_json(const ProbeResult *probe) {
  return written([&](JsonChunkWriter &w) { write_probe_json(w, probe); });
}

}  // namespace

TEST(SnifferData, BeforeAnyProbeTheProbeIsNull) { EXPECT_EQ(probe_json(nullptr), "null"); }

TEST(SnifferData, AProbeMatchesTheSpecExample) {
  const ProbeResult probe = spec_example_probe();
  EXPECT_EQ(probe_json(&probe), R"({"active":false,"kind":"probe","id":77,"value":"03","original":"01",)"
                                R"("steps":[{"kind":"get","outcome":"ok","value":"01","at":812000},)"
                                R"({"kind":"set","outcome":"ok","value":"03","at":812400}],)"
                                R"("verdict":"writable","restore":"ok"})");
}

TEST(SnifferData, AStartedProbeHasNoOriginalStepsVerdictOrRestore) {
  const ProbeResult probe = probe_start_result(ProbeKind::PROBE_KIND_PROBE, PROBE_ID, ProbeValue::of("03"));
  EXPECT_EQ(probe_json(&probe), R"({"active":true,"kind":"probe","id":77,"value":"03","original":null,)"
                                R"("steps":[],"verdict":null,"restore":null})");
}

TEST(SnifferData, ARestoreHasItsRequestedOriginalAndNoVerdict) {
  ProbeResult restore = probe_start_result(ProbeKind::PROBE_KIND_RESTORE, PROBE_ID, ProbeValue::of("01"));
  restore.active = false;
  restore.restore = ProbeRestore::PROBE_RESTORE_FAILED;
  EXPECT_EQ(probe_json(&restore), R"({"active":false,"kind":"restore","id":77,"value":"01","original":"01",)"
                                  R"("steps":[],"verdict":null,"restore":"failed"})");
}

TEST(SnifferData, AFailedGetStepHasANullValue) {
  ProbeResult probe = probe_start_result(ProbeKind::PROBE_KIND_PROBE, PROBE_ID, ProbeValue::of("03"));
  probe.steps[0] =
      ProbeStep{ProbeStepKind::PROBE_STEP_GET, ScanOutcome::SCAN_OUTCOME_TIMEOUT, false, ProbeValue{}, 1000};
  probe.steps[1] = ProbeStep{ProbeStepKind::PROBE_STEP_GET, ScanOutcome::SCAN_OUTCOME_NAK, false, ProbeValue{}, 2000};
  probe.step_count = 2;
  EXPECT_NE(probe_json(&probe).find(R"("steps":[{"kind":"get","outcome":"timeout","value":null,"at":1000},)"
                                    R"({"kind":"get","outcome":"nak","value":null,"at":2000}])"),
            std::string::npos);
}

TEST(SnifferData, EveryVerdictAndRestoreIsNamedAsTheSpecNamesIt) {
  ProbeResult probe = spec_example_probe();
  probe.verdict = ProbeVerdict::PROBE_VERDICT_NOT_WRITABLE;
  probe.restore = ProbeRestore::PROBE_RESTORE_SKIPPED;
  EXPECT_NE(probe_json(&probe).find(R"("verdict":"not writable","restore":"skipped"})"), std::string::npos);
  probe.verdict = ProbeVerdict::PROBE_VERDICT_UNCLEAR;
  EXPECT_NE(probe_json(&probe).find(R"("verdict":"unclear")"), std::string::npos);
  probe.verdict = ProbeVerdict::PROBE_VERDICT_NOTHING_WRITTEN;
  EXPECT_NE(probe_json(&probe).find(R"("verdict":"nothing written")"), std::string::npos);
}

// Encoded as in records: control and non-ASCII bytes as \u00XX.
TEST(SnifferData, ProbeValuesAreEscapedByteExact) {
  ProbeResult probe = probe_start_result(ProbeKind::PROBE_KIND_PROBE, PROBE_ID, ProbeValue::of("a\x01\xff"));
  probe.has_original = true;
  probe.original = ProbeValue::of("\"");
  probe.steps[0] = step(ProbeStepKind::PROBE_STEP_GET, std::string("\x1f\x80", 2), 0);
  probe.step_count = 1;
  const std::string json = probe_json(&probe);
  EXPECT_NE(json.find(R"("value":"a\u0001\u00ff","original":"\"")"), std::string::npos) << json;
  EXPECT_NE(json.find(R"("value":"\u001f\u0080")"), std::string::npos) << json;
}

TEST(SnifferData, TheDataResponseCarriesTheProbeAfterTheScan) {
  PropertyStore store = allocated_store();
  const ProbeResult probe = spec_example_probe();
  const std::string json = data_json(store, 0, NO_RUN, &probe);
  EXPECT_NE(json.find(R"("done":false},"probe":{"active":false,"kind":"probe","id":77,)"), std::string::npos) << json;
  EXPECT_NE(json.find(R"("restore":"ok"},"records":[)"), std::string::npos) << json;
}

// --- The 409 of a busy start ---

namespace {

std::string busy_json(const ScanState &scan, const ProbeResult *probe) {
  return written([&](JsonChunkWriter &w) { write_busy_json(w, scan, probe); });
}

}  // namespace

TEST(SnifferData, ABusyReplyDuringAScanCarriesTheScanAndANullProbe) {
  EXPECT_EQ(busy_json(scan_state(running(CHECK_RUN, FAN_LABEL_IDS.front())), nullptr),
            R"({"error":"busy","scan":)" + check_json(true, std::to_string(FAN_LABEL_IDS.front()), false) +
                R"(,"probe":null})");
}

TEST(SnifferData, ABusyReplyDuringAProbeCarriesTheProbe) {
  const ProbeResult probe = probe_start_result(ProbeKind::PROBE_KIND_PROBE, PROBE_ID, ProbeValue::of("03"));
  EXPECT_EQ(busy_json(scan_state(completed(CHECK_RUN)), &probe),
            R"({"error":"busy","scan":)" + check_json(false, "null", true) +
                R"(,"probe":{"active":true,"kind":"probe","id":77,"value":"03","original":null,)"
                R"("steps":[],"verdict":null,"restore":null}})");
}

TEST(SnifferData, ProbeAndRestoreAreRoutedAsPosts) {
  EXPECT_EQ(sniffer_route(SnifferMethod::METHOD_POST, SNIFFER_PROBE_PATH), SnifferRoute::ROUTE_PROBE);
  EXPECT_EQ(sniffer_route(SnifferMethod::METHOD_POST, SNIFFER_RESTORE_PATH), SnifferRoute::ROUTE_RESTORE);
  EXPECT_EQ(sniffer_route(SnifferMethod::METHOD_GET, SNIFFER_PROBE_PATH), SnifferRoute::ROUTE_NONE);
  EXPECT_EQ(sniffer_route(SnifferMethod::METHOD_GET, SNIFFER_RESTORE_PATH), SnifferRoute::ROUTE_NONE);
}

TEST(SnifferData, NoChangesGiveAnEmptyRecordList) {
  PropertyStore store = allocated_store();
  record(store, 1, "a", 1);

  EXPECT_NE(data_json(store, store.seq()).find(R"("records":[]})"), std::string::npos);
}

TEST(SnifferData, TheNameIsEscaped) {
  PropertyStore store = allocated_store();
  static RecordSnapshot scratch;
  FakeLock lock;
  std::string out;
  JsonChunkWriter w(&collect, &out);
  const DeviceInfo device{"m", "a\"b", "b", 0};
  write_data_json(w, store, device, NO_RUN, nullptr, 0, scratch, lock.with_lock(), [](int) { return false; });

  EXPECT_NE(out.find(R"("name":"a\"b")"), std::string::npos);
}

TEST(SnifferData, RegisteredIdsAreReadPerRecord) {
  PropertyStore store = allocated_store();
  record(store, 1, "a", 1);
  record(store, 2, "b", 1);
  static RecordSnapshot scratch;
  FakeLock lock;
  std::string out;
  JsonChunkWriter w(&collect, &out);
  write_data_json(w, store, DEVICE, NO_RUN, nullptr, 0, scratch, lock.with_lock(), [](int id) { return id == 2; });

  EXPECT_NE(out.find(R"("id":1,)"), std::string::npos);
  EXPECT_NE(out.find(R"("reg":false,"hist":[[1000,"a"]])"), std::string::npos);
  EXPECT_NE(out.find(R"("reg":true,"hist":[[1000,"b"]])"), std::string::npos);
}

struct LockedSink {
  FakeLock *lock;
  std::string out;
  int sends_under_lock = 0;
  static bool sink(void *context, const char *data, size_t len) {
    auto *self = static_cast<LockedSink *>(context);
    if (self->lock->held) {
      self->sends_under_lock++;
    }
    self->out.append(data, len);
    return true;
  }
};

TEST(SnifferData, NothingIsSentWhileTheLockIsHeld) {
  PropertyStore store = allocated_store();
  for (int id = 0; id <= MAX_PROPERTY_ID; id++) {
    record(store, id, "value " + std::to_string(id), 1);
  }
  static RecordSnapshot scratch;
  FakeLock lock;
  LockedSink sink{&lock};
  JsonChunkWriter w(&LockedSink::sink, &sink);

  EXPECT_TRUE(
      write_data_json(w, store, DEVICE, NO_RUN, nullptr, 0, scratch, lock.with_lock(), [](int) { return false; }));

  EXPECT_GT(sink.out.size(), 10 * JSON_CHUNK_BYTES);
  EXPECT_EQ(sink.sends_under_lock, 0);
  // The header, and each record: once per record plus the last look that finds none.
  EXPECT_EQ(lock.taken, 1 + MAX_PROPERTY_ID + 2);
  EXPECT_FALSE(lock.held);
}

TEST(SnifferData, AFailedSendIsReported) {
  PropertyStore store = allocated_store();
  record(store, 1, "a", 1);
  static RecordSnapshot scratch;
  FakeLock lock;
  JsonChunkWriter w([](void *, const char *, size_t) { return false; }, nullptr);

  EXPECT_FALSE(
      write_data_json(w, store, DEVICE, NO_RUN, nullptr, 0, scratch, lock.with_lock(), [](int) { return false; }));
}

// --- Request parsing ---

TEST(SnifferData, SinceIsReadFromTheQuery) {
  EXPECT_EQ(since_from_uri("/sniffer/data?since=1204"), 1204u);
  EXPECT_EQ(since_from_uri("/sniffer/data?x=1&since=7&y=2"), 7u);
  EXPECT_EQ(since_from_uri("/sniffer/data?since=4294967295"), 4294967295u);
}

TEST(SnifferData, AMissingOrInvalidSinceMeansAFullResponse) {
  EXPECT_EQ(since_from_uri("/sniffer/data"), 0u);
  EXPECT_EQ(since_from_uri("/sniffer/data?"), 0u);
  EXPECT_EQ(since_from_uri("/sniffer/data?since="), 0u);
  EXPECT_EQ(since_from_uri("/sniffer/data?since=12x"), 0u);
  EXPECT_EQ(since_from_uri("/sniffer/data?since=-1"), 0u);
  EXPECT_EQ(since_from_uri("/sniffer/data?since=4294967296"), 0u);
  EXPECT_EQ(since_from_uri("/sniffer/data?xsince=5"), 0u);
}

TEST(SnifferData, RequestsAreRoutedByMethodAndPath) {
  EXPECT_EQ(sniffer_route(SnifferMethod::METHOD_GET, SNIFFER_PAGE_PATH), SnifferRoute::ROUTE_PAGE);
  EXPECT_EQ(sniffer_route(SnifferMethod::METHOD_GET, SNIFFER_DATA_PATH), SnifferRoute::ROUTE_DATA);
  EXPECT_EQ(sniffer_route(SnifferMethod::METHOD_POST, SNIFFER_SCAN_START_PATH), SnifferRoute::ROUTE_SCAN_START);
  EXPECT_EQ(sniffer_route(SnifferMethod::METHOD_POST, SNIFFER_SCAN_STOP_PATH), SnifferRoute::ROUTE_SCAN_STOP);
  EXPECT_EQ(sniffer_route(SnifferMethod::METHOD_POST, SNIFFER_CLEAR_PATH), SnifferRoute::ROUTE_CLEAR);
}

TEST(SnifferData, AFullResponseAfterAClearHasNoRecords) {
  PropertyStore store = allocated_store();
  record_spec_example(store);

  store.clear();

  EXPECT_EQ(next_record_in_delta(store, 0, 0), DELTA_END);
}

TEST(SnifferData, OtherRequestsAreLeftToOtherHandlers) {
  EXPECT_EQ(sniffer_route(SnifferMethod::METHOD_POST, SNIFFER_DATA_PATH), SnifferRoute::ROUTE_NONE);
  EXPECT_EQ(sniffer_route(SnifferMethod::METHOD_GET, SNIFFER_SCAN_START_PATH), SnifferRoute::ROUTE_NONE);
  EXPECT_EQ(sniffer_route(SnifferMethod::METHOD_GET, SNIFFER_CLEAR_PATH), SnifferRoute::ROUTE_NONE);
  EXPECT_EQ(sniffer_route(SnifferMethod::METHOD_OTHER, SNIFFER_PAGE_PATH), SnifferRoute::ROUTE_NONE);
  EXPECT_EQ(sniffer_route(SnifferMethod::METHOD_GET, "/"), SnifferRoute::ROUTE_NONE);
  EXPECT_EQ(sniffer_route(SnifferMethod::METHOD_GET, "/sniffer/"), SnifferRoute::ROUTE_NONE);
  EXPECT_EQ(sniffer_route(SnifferMethod::METHOD_GET, "/sniffer/datax"), SnifferRoute::ROUTE_NONE);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
