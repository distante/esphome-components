#include <gtest/gtest.h>

#include "sec_touch/scan_outcome.h"

using esphome::sec_touch::ScanOutcome;
using esphome::sec_touch::ScanOutcomeTracker;
using esphome::sec_touch::TaskEnd;
using esphome::sec_touch::task_end_name;

TEST(ScanOutcomeTracker, NakDuringAScanIsNak) {
  ScanOutcomeTracker tracker;

  tracker.record(TaskEnd::TASK_END_NAK, /*scan_mode=*/true, /*sniffer_task=*/true);

  EXPECT_EQ(tracker.last(), ScanOutcome::SCAN_OUTCOME_NAK);
}

TEST(ScanOutcomeTracker, NoReplyDuringAScanIsTimeout) {
  ScanOutcomeTracker tracker;

  tracker.record(TaskEnd::TASK_END_TIMEOUT, /*scan_mode=*/true, /*sniffer_task=*/true);

  EXPECT_EQ(tracker.last(), ScanOutcome::SCAN_OUTCOME_TIMEOUT);
}

TEST(ScanOutcomeTracker, AnAnswerAfterAFailedQueryIsOk) {
  ScanOutcomeTracker tracker;
  tracker.record(TaskEnd::TASK_END_TIMEOUT, /*scan_mode=*/true, /*sniffer_task=*/true);

  tracker.record(TaskEnd::TASK_END_ANSWERED, /*scan_mode=*/true, /*sniffer_task=*/true);

  EXPECT_EQ(tracker.last(), ScanOutcome::SCAN_OUTCOME_OK);
}

// An unparseable reply is no usable answer, so the sniffer retries it like a timeout.
TEST(ScanOutcomeTracker, AnUnparseableReplyDuringAScanIsTimeout) {
  ScanOutcomeTracker tracker;

  tracker.record(TaskEnd::TASK_END_MALFORMED, /*scan_mode=*/true, /*sniffer_task=*/true);

  EXPECT_EQ(tracker.last(), ScanOutcome::SCAN_OUTCOME_TIMEOUT);
}

TEST(ScanOutcomeTracker, TasksOutsideScanModeLeaveTheOutcomeAlone) {
  ScanOutcomeTracker tracker;
  tracker.record(TaskEnd::TASK_END_NAK, /*scan_mode=*/true, /*sniffer_task=*/true);

  tracker.record(TaskEnd::TASK_END_TIMEOUT, /*scan_mode=*/false, /*sniffer_task=*/true);
  tracker.record(TaskEnd::TASK_END_ANSWERED, /*scan_mode=*/false, /*sniffer_task=*/true);

  EXPECT_EQ(tracker.last(), ScanOutcome::SCAN_OUTCOME_NAK);
}

// Writes stay allowed during a scan: a time-program SET or a fan SET that ends after a scan query must not replace
// that query's outcome.
TEST(ScanOutcomeTracker, TasksTheSnifferDidNotQueueLeaveTheOutcomeAlone) {
  ScanOutcomeTracker tracker;
  tracker.record(TaskEnd::TASK_END_TIMEOUT, /*scan_mode=*/true, /*sniffer_task=*/true);

  tracker.record(TaskEnd::TASK_END_ANSWERED, /*scan_mode=*/true, /*sniffer_task=*/false);
  tracker.record(TaskEnd::TASK_END_NAK, /*scan_mode=*/true, /*sniffer_task=*/false);

  EXPECT_EQ(tracker.last(), ScanOutcome::SCAN_OUTCOME_TIMEOUT);
}

// A scan stopped during the timeout retry wait must not hand that timeout to the next scan.
TEST(ScanOutcomeTracker, StartingAScanForgetsThePreviousScansOutcome) {
  ScanOutcomeTracker tracker;
  tracker.record(TaskEnd::TASK_END_TIMEOUT, /*scan_mode=*/true, /*sniffer_task=*/true);

  tracker.begin_scan();

  EXPECT_EQ(tracker.last(), ScanOutcome::SCAN_OUTCOME_OK);
}

// The hub logs why a task ended; each end gets its own name.
TEST(TaskEndName, NamesEveryTaskEnd) {
  EXPECT_STREQ(task_end_name(TaskEnd::TASK_END_ANSWERED), "answered");
  EXPECT_STREQ(task_end_name(TaskEnd::TASK_END_NAK), "nak");
  EXPECT_STREQ(task_end_name(TaskEnd::TASK_END_MALFORMED), "malformed");
  EXPECT_STREQ(task_end_name(TaskEnd::TASK_END_TIMEOUT), "timeout");
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
