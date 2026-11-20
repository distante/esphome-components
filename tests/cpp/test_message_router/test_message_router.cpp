#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "sec_touch/message_router.h"
#include "sec_touch/protocol_ids.h"

using esphome::sec_touch::COMMANDID_SET;
using esphome::sec_touch::FAN_LEVEL_IDS;
using esphome::sec_touch::MessageRouter;
using esphome::sec_touch::Route;

namespace {

/// An id an entity registers: fan pair 1's level property.
constexpr int REGISTERED_PROPERTY_ID = FAN_LEVEL_IDS[0];

/// An id no entity registers.
constexpr int UNREGISTERED_PROPERTY_ID = 5;

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

  Route route = router.dispatch(COMMANDID_SET, UNREGISTERED_PROPERTY_ID, "0100", /*scan_mode=*/false,
                                /*has_entity_listener=*/false);

  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0].command_id, COMMANDID_SET);
  EXPECT_EQ(calls[0].property_id, UNREGISTERED_PROPERTY_ID);
  EXPECT_EQ(calls[0].raw_value, "0100");
  EXPECT_EQ(route, Route::DROP);
}

TEST(MessageRouter, TapSeesARegisteredIdInPassiveModeAndListenersStillGetIt) {
  MessageRouter router;
  std::vector<TapCall> calls;
  router.add_tap([&](int cmd, int id, const std::string &raw) { calls.push_back({cmd, id, raw}); });

  Route route =
      router.dispatch(COMMANDID_SET, REGISTERED_PROPERTY_ID, "3", /*scan_mode=*/false, /*has_entity_listener=*/true);

  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0].property_id, REGISTERED_PROPERTY_ID);
  EXPECT_EQ(calls[0].raw_value, "3");
  EXPECT_EQ(route, Route::DELIVER);
}

TEST(MessageRouter, ScanModeStopsRoutingAfterTheTap) {
  MessageRouter router;
  std::vector<TapCall> calls;
  router.add_tap([&](int cmd, int id, const std::string &raw) { calls.push_back({cmd, id, raw}); });

  Route route =
      router.dispatch(COMMANDID_SET, REGISTERED_PROPERTY_ID, "3", /*scan_mode=*/true, /*has_entity_listener=*/true);

  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0].property_id, REGISTERED_PROPERTY_ID);
  EXPECT_EQ(route, Route::DROP);
}

TEST(MessageRouter, WarnsOnlyWhenNoListenerAndNoTap) {
  MessageRouter router;

  EXPECT_EQ(router.dispatch(COMMANDID_SET, UNREGISTERED_PROPERTY_ID, "0100", /*scan_mode=*/false,
                            /*has_entity_listener=*/false),
            Route::WARN_NO_LISTENER);

  router.add_tap([](int, int, const std::string &) {});

  EXPECT_EQ(router.dispatch(COMMANDID_SET, UNREGISTERED_PROPERTY_ID, "0100", /*scan_mode=*/false,
                            /*has_entity_listener=*/false),
            Route::DROP);
}

TEST(MessageRouter, EveryTapSeesEveryMessage) {
  MessageRouter router;
  std::vector<int> first;
  std::vector<int> second;
  router.add_tap([&](int, int id, const std::string &) { first.push_back(id); });
  router.add_tap([&](int, int id, const std::string &) { second.push_back(id); });

  router.dispatch(COMMANDID_SET, UNREGISTERED_PROPERTY_ID, "0100", /*scan_mode=*/false, /*has_entity_listener=*/false);
  router.dispatch(COMMANDID_SET, REGISTERED_PROPERTY_ID, "3", /*scan_mode=*/false, /*has_entity_listener=*/true);

  EXPECT_EQ(first, (std::vector<int>{UNREGISTERED_PROPERTY_ID, REGISTERED_PROPERTY_ID}));
  EXPECT_EQ(second, (std::vector<int>{UNREGISTERED_PROPERTY_ID, REGISTERED_PROPERTY_ID}));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
