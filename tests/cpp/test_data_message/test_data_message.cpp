#include <gtest/gtest.h>

#include <string>

#include "../common/wire_message.h"
#include "sec_touch/data_message.h"

using esphome::sec_touch::check_data_message;
using esphome::sec_touch::data_message_check_reason;
using esphome::sec_touch::DataMessageCheck;
using esphome::sec_touch::find_data_message_tabs;
using sec_touch_test::wire_message;

namespace {

// Checks [STX]body[ETX], where body uses '\t' for TAB.
DataMessageCheck check(const std::string &body, bool optional_crc) {
  std::string msg = wire_message(body);
  int len = static_cast<int>(msg.size());
  return check_data_message(msg.data(), len, find_data_message_tabs(msg.data(), len), optional_crc);
}

// Messages captured on the installed controller (issue #43).
const std::string GOOD = "32\t173\t10\t42696";
const std::string CORRUPTED = "32\t133\t10\t42696";  // the reply for 173, '7' -> '3'
const std::string LOST_TAB = "32176\t0\t58673";      // the reply for 176, first TAB lost
const std::string NO_CRC = "32\t173\t10";

}  // namespace

TEST(FindDataMessageTabs, FindsTheFirstThreeTabs) {
  std::string msg = wire_message(GOOD);
  auto tabs = find_data_message_tabs(msg.data(), static_cast<int>(msg.size()));

  EXPECT_EQ(tabs.tab1, 3);
  EXPECT_EQ(tabs.tab2, 7);
  EXPECT_EQ(tabs.tab3, 10);
}

TEST(FindDataMessageTabs, MarksMissingTabsAsMinusOne) {
  std::string msg = wire_message("32\t173");
  auto tabs = find_data_message_tabs(msg.data(), static_cast<int>(msg.size()));

  EXPECT_EQ(tabs.tab1, 3);
  EXPECT_EQ(tabs.tab2, -1);
  EXPECT_EQ(tabs.tab3, -1);
}

// A CRC that is there is checked, whatever optional_crc says.
TEST(CheckDataMessage, RoutesGoodCrcWithFlagOff) { EXPECT_EQ(check(GOOD, false), DataMessageCheck::DATA_ROUTE); }
TEST(CheckDataMessage, RoutesGoodCrcWithFlagOn) { EXPECT_EQ(check(GOOD, true), DataMessageCheck::DATA_ROUTE); }
TEST(CheckDataMessage, RejectsBadCrcWithFlagOff) {
  EXPECT_EQ(check(CORRUPTED, false), DataMessageCheck::DATA_CRC_MISMATCH);
}
TEST(CheckDataMessage, RejectsBadCrcWithFlagOn) {
  EXPECT_EQ(check(CORRUPTED, true), DataMessageCheck::DATA_CRC_MISMATCH);
}
TEST(CheckDataMessage, RejectsNonNumericCrcWithFlagOn) {
  EXPECT_EQ(check("32\t173\t10\t42a96", true), DataMessageCheck::DATA_CRC_NOT_A_NUMBER);
}

// optional_crc only decides what happens when the CRC field is missing.
TEST(CheckDataMessage, RejectsMissingCrcWithFlagOff) {
  EXPECT_EQ(check(NO_CRC, false), DataMessageCheck::DATA_MISSING_FIELDS);
}
TEST(CheckDataMessage, RoutesMissingCrcUncheckedWithFlagOn) {
  EXPECT_EQ(check(NO_CRC, true), DataMessageCheck::DATA_ROUTE_UNCHECKED);
}

// The lost-TAB message looks like a message without a CRC field: only optional_crc: false catches it.
TEST(CheckDataMessage, RejectsLostTabWithFlagOff) {
  EXPECT_EQ(check(LOST_TAB, false), DataMessageCheck::DATA_MISSING_FIELDS);
}
TEST(CheckDataMessage, CannotCatchLostTabWithFlagOn) {
  EXPECT_EQ(check(LOST_TAB, true), DataMessageCheck::DATA_ROUTE_UNCHECKED);
}

// No property ID is malformed whatever optional_crc says.
TEST(CheckDataMessage, RejectsMissingPropertyWithFlagOff) {
  EXPECT_EQ(check("32\t173", false), DataMessageCheck::DATA_MISSING_FIELDS);
}
TEST(CheckDataMessage, RejectsMissingPropertyWithFlagOn) {
  EXPECT_EQ(check("32\t173", true), DataMessageCheck::DATA_MISSING_FIELDS);
}

// The reason the hub logs when it drops a message.
TEST(DataMessageCheckReason, NamesEachRejection) {
  EXPECT_STREQ(data_message_check_reason(DataMessageCheck::DATA_MISSING_FIELDS), "Not enough TABs in message");
  EXPECT_STREQ(data_message_check_reason(DataMessageCheck::DATA_CRC_MISMATCH), "CRC mismatch");
  EXPECT_STREQ(data_message_check_reason(DataMessageCheck::DATA_CRC_NOT_A_NUMBER), "CRC field is not a number");
}
TEST(DataMessageCheckReason, IsEmptyForRoutedMessages) {
  EXPECT_STREQ(data_message_check_reason(DataMessageCheck::DATA_ROUTE), "");
  EXPECT_STREQ(data_message_check_reason(DataMessageCheck::DATA_ROUTE_UNCHECKED), "");
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
