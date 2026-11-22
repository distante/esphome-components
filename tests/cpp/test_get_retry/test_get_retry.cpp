#include <gtest/gtest.h>

#include "sec_touch/get_retry.h"

using esphome::sec_touch::DataMessageCheck;
using esphome::sec_touch::should_retry_get;

namespace {

constexpr bool GET = true;
constexpr bool SET = false;
constexpr bool FIRST_TRY = false;
constexpr bool ALREADY_RETRIED = true;
constexpr bool POLLING = false;
constexpr bool SCAN_MODE = true;

}  // namespace

// Every check that drops a data frame (issue #45) earns the GET one more send.
TEST(ShouldRetryGet, AGetWhoseReplyFailedTheCrcIsRetried) {
  EXPECT_TRUE(should_retry_get(GET, DataMessageCheck::DATA_CRC_MISMATCH, FIRST_TRY, POLLING));
  EXPECT_TRUE(should_retry_get(GET, DataMessageCheck::DATA_CRC_NOT_A_NUMBER, FIRST_TRY, POLLING));
}

TEST(ShouldRetryGet, AGetWhoseReplyMissedFieldsIsRetried) {
  EXPECT_TRUE(should_retry_get(GET, DataMessageCheck::DATA_MISSING_FIELDS, FIRST_TRY, POLLING));
}

// A second bad reply ends the task, so a noisy line can't loop.
TEST(ShouldRetryGet, ARetriedGetIsNotRetriedAgain) {
  EXPECT_FALSE(should_retry_get(GET, DataMessageCheck::DATA_CRC_MISMATCH, ALREADY_RETRIED, POLLING));
}

// The sniffer retries a scan query on its own, after its TIMEOUT outcome.
TEST(ShouldRetryGet, NoRetryInScanMode) {
  EXPECT_FALSE(should_retry_get(GET, DataMessageCheck::DATA_CRC_MISMATCH, FIRST_TRY, SCAN_MODE));
}

TEST(ShouldRetryGet, ASetIsNotRetried) {
  EXPECT_FALSE(should_retry_get(SET, DataMessageCheck::DATA_CRC_MISMATCH, FIRST_TRY, POLLING));
}

// A reply the hub routes was not dropped.
TEST(ShouldRetryGet, ARoutedReplyIsNotRetried) {
  EXPECT_FALSE(should_retry_get(GET, DataMessageCheck::DATA_ROUTE, FIRST_TRY, POLLING));
  EXPECT_FALSE(should_retry_get(GET, DataMessageCheck::DATA_ROUTE_UNCHECKED, FIRST_TRY, POLLING));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
