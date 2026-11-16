#include <gtest/gtest.h>

#include "sec_touch/scan_outcome.h"

using esphome::sec_touch::ScanOutcome;
using esphome::sec_touch::ScanOutcomeTracker;
using esphome::sec_touch::TaskEnd;

TEST(ScanOutcomeTracker, NakDuringAScanIsNak) {
  ScanOutcomeTracker tracker;

  tracker.record(TaskEnd::TASK_END_NAK, /*scan_mode=*/true);

  EXPECT_EQ(tracker.last(), ScanOutcome::SCAN_OUTCOME_NAK);
}

TEST(ScanOutcomeTracker, NoReplyDuringAScanIsTimeout) {
  ScanOutcomeTracker tracker;

  tracker.record(TaskEnd::TASK_END_TIMEOUT, /*scan_mode=*/true);

  EXPECT_EQ(tracker.last(), ScanOutcome::SCAN_OUTCOME_TIMEOUT);
}

TEST(ScanOutcomeTracker, AnAnswerAfterAFailedQueryIsOk) {
  ScanOutcomeTracker tracker;
  tracker.record(TaskEnd::TASK_END_TIMEOUT, /*scan_mode=*/true);

  tracker.record(TaskEnd::TASK_END_ANSWERED, /*scan_mode=*/true);

  EXPECT_EQ(tracker.last(), ScanOutcome::SCAN_OUTCOME_OK);
}

// An unparseable reply is no usable answer, so the sniffer retries it like a timeout.
TEST(ScanOutcomeTracker, AnUnparseableReplyDuringAScanIsTimeout) {
  ScanOutcomeTracker tracker;

  tracker.record(TaskEnd::TASK_END_MALFORMED, /*scan_mode=*/true);

  EXPECT_EQ(tracker.last(), ScanOutcome::SCAN_OUTCOME_TIMEOUT);
}

TEST(ScanOutcomeTracker, TasksOutsideScanModeLeaveTheOutcomeAlone) {
  ScanOutcomeTracker tracker;
  tracker.record(TaskEnd::TASK_END_NAK, /*scan_mode=*/true);

  tracker.record(TaskEnd::TASK_END_TIMEOUT, /*scan_mode=*/false);
  tracker.record(TaskEnd::TASK_END_ANSWERED, /*scan_mode=*/false);

  EXPECT_EQ(tracker.last(), ScanOutcome::SCAN_OUTCOME_NAK);
}

// A scan stopped during the timeout retry wait must not hand that timeout to the next scan.
TEST(ScanOutcomeTracker, StartingAScanForgetsThePreviousScansOutcome) {
  ScanOutcomeTracker tracker;
  tracker.record(TaskEnd::TASK_END_TIMEOUT, /*scan_mode=*/true);

  tracker.begin_scan();

  EXPECT_EQ(tracker.last(), ScanOutcome::SCAN_OUTCOME_OK);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
