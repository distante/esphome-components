#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "sec_touch/message_router.h"

using esphome::sec_touch::MessageRouter;
using esphome::sec_touch::Route;

namespace {

struct TapCall {
  int command_id;
  int property_id;
  std::string raw_value;
};

}  // namespace

TEST(MessageRouter, TapSeesAnUnregisteredIdInPassiveMode) {
  MessageRouter router;
  std::vector<TapCall> calls;
  router.add_tap([&](int cmd, int id, const std::string &raw) { calls.push_back({cmd, id, raw}); });

  Route route = router.dispatch(32, 5, "0100", /*scan_mode=*/false, /*has_entity_listener=*/false);

  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0].command_id, 32);
  EXPECT_EQ(calls[0].property_id, 5);
  EXPECT_EQ(calls[0].raw_value, "0100");
  EXPECT_EQ(route, Route::DROP);
}

TEST(MessageRouter, TapSeesARegisteredIdInPassiveModeAndListenersStillGetIt) {
  MessageRouter router;
  std::vector<TapCall> calls;
  router.add_tap([&](int cmd, int id, const std::string &raw) { calls.push_back({cmd, id, raw}); });

  Route route = router.dispatch(32, 173, "3", /*scan_mode=*/false, /*has_entity_listener=*/true);

  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0].property_id, 173);
  EXPECT_EQ(calls[0].raw_value, "3");
  EXPECT_EQ(route, Route::DELIVER);
}

TEST(MessageRouter, ScanModeStopsRoutingAfterTheTap) {
  MessageRouter router;
  std::vector<TapCall> calls;
  router.add_tap([&](int cmd, int id, const std::string &raw) { calls.push_back({cmd, id, raw}); });

  Route route = router.dispatch(32, 173, "3", /*scan_mode=*/true, /*has_entity_listener=*/true);

  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0].property_id, 173);
  EXPECT_EQ(route, Route::DROP);
}

TEST(MessageRouter, WarnsOnlyWhenNoListenerAndNoTap) {
  MessageRouter router;

  EXPECT_EQ(router.dispatch(32, 5, "0100", /*scan_mode=*/false, /*has_entity_listener=*/false),
            Route::WARN_NO_LISTENER);
  EXPECT_EQ(router.dispatch(32, 173, "3", /*scan_mode=*/false, /*has_entity_listener=*/true), Route::DELIVER);
}

TEST(MessageRouter, EveryTapSeesEveryMessage) {
  MessageRouter router;
  std::vector<int> first;
  std::vector<int> second;
  router.add_tap([&](int, int id, const std::string &) { first.push_back(id); });
  router.add_tap([&](int, int id, const std::string &) { second.push_back(id); });

  router.dispatch(32, 5, "0100", /*scan_mode=*/false, /*has_entity_listener=*/false);
  router.dispatch(32, 173, "3", /*scan_mode=*/false, /*has_entity_listener=*/true);

  EXPECT_EQ(first, (std::vector<int>{5, 173}));
  EXPECT_EQ(second, (std::vector<int>{5, 173}));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
