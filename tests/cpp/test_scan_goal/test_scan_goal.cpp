#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "sec_touch/known_property_meaning.h"
#include "sec_touch/protocol_ids.h"
#include "sec_touch_sniffer/property_slots.h"
#include "sec_touch_sniffer/scan_goal.h"

using esphome::sec_touch::CHECK_RUN;
using esphome::sec_touch::DEFAULT_DISCOVER_FROM;
using esphome::sec_touch::DEFAULT_DISCOVER_RUN;
using esphome::sec_touch::DEFAULT_DISCOVER_TO;
using esphome::sec_touch::FAN_LABEL_IDS;
using esphome::sec_touch::FAN_LEVEL_IDS;
using esphome::sec_touch::first_scan_id;
using esphome::sec_touch::GLOBAL_SETTING_IDS;
using esphome::sec_touch::is_known_property;
using esphome::sec_touch::last_scan_id;
using esphome::sec_touch::MAX_PROPERTY_ID;
using esphome::sec_touch::next_scan_id;
using esphome::sec_touch::NO_SCAN_ID;
using esphome::sec_touch::query_value;
using esphome::sec_touch::SCAN_GOAL_CHECK_NAME;
using esphome::sec_touch::SCAN_GOAL_DISCOVER_NAME;
using esphome::sec_touch::scan_goal_name;
using esphome::sec_touch::scan_start_from_uri;
using esphome::sec_touch::ScanGoal;
using esphome::sec_touch::ScanRun;
using esphome::sec_touch::ScanStart;
using esphome::sec_touch::SUMMER_VENTILATION_PROPERTY_ID;

namespace {

ScanRun discover(int from, int to) {
  return ScanRun{ScanGoal::SCAN_GOAL_DISCOVER, static_cast<uint8_t>(from), static_cast<uint8_t>(to)};
}

/// Every ID a run queries, in the order the sniffer steps through them.
std::vector<int> run_ids(const ScanRun &run) {
  std::vector<int> ids;
  for (int id = first_scan_id(run); id != NO_SCAN_ID; id = next_scan_id(run, id + 1)) {
    ids.push_back(id);
  }
  return ids;
}

/// The catalogue's known IDs, ascending, straight from its arrays.
std::vector<int> known_ids_ascending() {
  std::vector<int> ids(FAN_LEVEL_IDS.begin(), FAN_LEVEL_IDS.end());
  ids.insert(ids.end(), FAN_LABEL_IDS.begin(), FAN_LABEL_IDS.end());
  ids.insert(ids.end(), GLOBAL_SETTING_IDS.begin(), GLOBAL_SETTING_IDS.end());
  std::sort(ids.begin(), ids.end());
  return ids;
}

bool same_run(const ScanRun &a, const ScanRun &b) { return a.goal == b.goal && a.from == b.from && a.to == b.to; }

void expect_start(const std::string &query, const ScanRun &expected) {
  const ScanStart start = scan_start_from_uri("/sniffer/scan/start" + query);
  EXPECT_TRUE(start.valid) << query;
  EXPECT_TRUE(same_run(start.run, expected))
      << query << ": goal " << scan_goal_name(start.run.goal) << " " << int(start.run.from) << "-" << int(start.run.to);
}

void expect_bad_range(const std::string &query) {
  EXPECT_FALSE(scan_start_from_uri("/sniffer/scan/start" + query).valid) << query;
}

}  // namespace

// --- The goal's ID sequence ---

TEST(ScanGoal, CheckQueriesExactlyTheKnownIdsAscending) { EXPECT_EQ(run_ids(CHECK_RUN), known_ids_ascending()); }

TEST(ScanGoal, CheckIgnoresTheRange) {
  EXPECT_EQ(run_ids(ScanRun{ScanGoal::SCAN_GOAL_CHECK, 100, 101}), known_ids_ascending());
}

TEST(ScanGoal, DiscoverQueriesItsRangeMinusTheKnownIds) {
  std::vector<int> expected;
  for (int id = DEFAULT_DISCOVER_FROM; id <= DEFAULT_DISCOVER_TO; id++) {
    const std::vector<int> known = known_ids_ascending();
    if (std::find(known.begin(), known.end(), id) == known.end()) {
      expected.push_back(id);
    }
  }
  EXPECT_EQ(run_ids(DEFAULT_DISCOVER_RUN), expected);
}

TEST(ScanGoal, DiscoverSkipsAKnownIdInsideItsRange) {
  EXPECT_EQ(run_ids(discover(SUMMER_VENTILATION_PROPERTY_ID - 1, SUMMER_VENTILATION_PROPERTY_ID + 1)),
            (std::vector<int>{SUMMER_VENTILATION_PROPERTY_ID - 1, SUMMER_VENTILATION_PROPERTY_ID + 1}));
}

TEST(ScanGoal, DiscoverCoversBothEndsOfItsRange) {
  EXPECT_EQ(run_ids(discover(0, 0)), std::vector<int>{0});
  EXPECT_EQ(run_ids(discover(MAX_PROPERTY_ID, MAX_PROPERTY_ID)), std::vector<int>{MAX_PROPERTY_ID});
}

TEST(ScanGoal, ADiscoverOfOnlyKnownIdsQueriesNothing) {
  const ScanRun run = discover(FAN_LABEL_IDS.front(), FAN_LABEL_IDS.back());
  EXPECT_EQ(first_scan_id(run), NO_SCAN_ID);
  EXPECT_EQ(last_scan_id(run), NO_SCAN_ID);
}

TEST(ScanGoal, TheLastIdIsTheLastOneTheRunQueries) {
  EXPECT_EQ(last_scan_id(CHECK_RUN), known_ids_ascending().back());
  EXPECT_EQ(last_scan_id(discover(FAN_LEVEL_IDS.front() - 1, FAN_LEVEL_IDS.back())), FAN_LEVEL_IDS.front() - 1);
}

// No resume: a run's first ID depends on the run alone, so a start after a stop begins there again.
TEST(ScanGoal, EveryRunBeginsAtItsGoalsFirstId) {
  EXPECT_EQ(first_scan_id(CHECK_RUN), known_ids_ascending().front());
  EXPECT_FALSE(is_known_property(DEFAULT_DISCOVER_FROM + 2));
  EXPECT_EQ(first_scan_id(discover(DEFAULT_DISCOVER_FROM + 2, MAX_PROPERTY_ID)), DEFAULT_DISCOVER_FROM + 2);
}

TEST(ScanGoal, PastTheLastIdNoneIsLeft) {
  EXPECT_EQ(next_scan_id(CHECK_RUN, known_ids_ascending().back() + 1), NO_SCAN_ID);
  EXPECT_EQ(next_scan_id(discover(5, 6), 7), NO_SCAN_ID);
}

TEST(ScanGoal, GoalsAreNamedAsThePageNamesThem) {
  EXPECT_STREQ(scan_goal_name(ScanGoal::SCAN_GOAL_DISCOVER), SCAN_GOAL_DISCOVER_NAME);
  EXPECT_STREQ(scan_goal_name(ScanGoal::SCAN_GOAL_CHECK), SCAN_GOAL_CHECK_NAME);
}

// --- The start request ---

TEST(ScanGoal, AStartWithoutGoalOrRangeDiscoversTheDefaultRange) {
  expect_start("", DEFAULT_DISCOVER_RUN);
  expect_start("?", DEFAULT_DISCOVER_RUN);
  EXPECT_EQ(DEFAULT_DISCOVER_FROM, 1);
  EXPECT_EQ(DEFAULT_DISCOVER_TO, MAX_PROPERTY_ID);
}

TEST(ScanGoal, AMissingOrUnknownGoalMeansDiscover) {
  expect_start("?goal=discover", DEFAULT_DISCOVER_RUN);
  expect_start("?goal=", DEFAULT_DISCOVER_RUN);
  expect_start("?goal=CHECK", DEFAULT_DISCOVER_RUN);
  expect_start("?goal=checks", DEFAULT_DISCOVER_RUN);
  expect_start("?from=3&to=9", discover(3, 9));
}

TEST(ScanGoal, AKeyThatOnlyStartsWithAParameterNameIsNotThatParameter) {
  EXPECT_EQ(query_value("?token=5&to=9", "to"), "9");
  expect_start("?goals=check", DEFAULT_DISCOVER_RUN);
  expect_start("?from=3&token=5&to=9", discover(3, 9));
}

TEST(ScanGoal, GoalCheckStartsACheck) {
  expect_start("?goal=check", CHECK_RUN);
  expect_start("?from=3&goal=check&to=9", CHECK_RUN);
}

TEST(ScanGoal, CheckIgnoresABadRange) {
  expect_start("?goal=check&from=9", CHECK_RUN);
  expect_start("?goal=check&from=x&to=300", CHECK_RUN);
  expect_start("?goal=check&from=9&to=3", CHECK_RUN);
}

TEST(ScanGoal, DiscoverTakesAnExplicitRange) {
  expect_start("?goal=discover&from=3&to=208", discover(3, 208));
  expect_start("?goal=discover&from=0&to=0", discover(0, 0));
  expect_start("?goal=discover&from=0&to=255", discover(0, MAX_PROPERTY_ID));
  expect_start("?goal=discover&from=255&to=255", discover(MAX_PROPERTY_ID, MAX_PROPERTY_ID));
  expect_start("?goal=discover&from=007&to=08", discover(7, 8));
}

TEST(ScanGoal, APartialRangeIsBad) {
  expect_bad_range("?goal=discover&from=3");
  expect_bad_range("?goal=discover&to=9");
  expect_bad_range("?from=3");
  expect_bad_range("?to=9");
}

TEST(ScanGoal, ANonNumericOrEmptyBoundIsBad) {
  expect_bad_range("?from=x&to=9");
  expect_bad_range("?from=3&to=9x");
  expect_bad_range("?from=&to=9");
  expect_bad_range("?from=3&to=");
  expect_bad_range("?from=+3&to=9");
  expect_bad_range("?from=3%20&to=9");
}

TEST(ScanGoal, ANegativeBoundIsBad) { expect_bad_range("?from=-1&to=9"); }

TEST(ScanGoal, ABoundAbove255IsBad) {
  expect_bad_range("?from=3&to=256");
  expect_bad_range("?from=256&to=300");
  expect_bad_range("?from=3&to=99999999999999999999");
}

TEST(ScanGoal, FromAboveToIsBad) { expect_bad_range("?from=9&to=3"); }

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
