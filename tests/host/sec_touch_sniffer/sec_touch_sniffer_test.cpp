#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "../sec_touch/common.h"
#include "esphome/components/sec_touch_sniffer/sec_touch_sniffer.h"
#include "esphome/components/sec_touch_sniffer/scan_goal.h"
#include "esphome/components/sec_touch_sniffer/sec_touch_sniffer_scan_switch.h"
#include "esphome/components/sec_touch_sniffer/sniffer_state.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"

namespace esphome::sec_touch::testing {

namespace {

/// An ID no entity registers.
constexpr int UNREGISTERED_ID = 133;
/// An ID an entity registers: fan pair 1's level.
constexpr int REGISTERED_ID = FAN_LEVEL_IDS[0];

/// The text sensor state listing UNREGISTERED_ID with value "10".
const std::string UNREGISTERED_STATE = std::to_string(UNREGISTERED_ID) + "=10";

/// Another ID no entity registers, scanned right after UNREGISTERED_ID.
constexpr int NEXT_UNREGISTERED_ID = UNREGISTERED_ID + 1;

ScanRun discover(int from, int to) {
  return ScanRun{ScanGoal::SCAN_GOAL_DISCOVER, static_cast<uint8_t>(from), static_cast<uint8_t>(to)};
}

/// The catalogue's known IDs, ascending, straight from its arrays.
std::vector<int> known_ids_ascending() {
  std::vector<int> ids(FAN_LEVEL_IDS.begin(), FAN_LEVEL_IDS.end());
  ids.insert(ids.end(), FAN_LABEL_IDS.begin(), FAN_LABEL_IDS.end());
  ids.insert(ids.end(), GLOBAL_SETTING_IDS.begin(), GLOBAL_SETTING_IDS.end());
  std::sort(ids.begin(), ids.end());
  return ids;
}

/// A hub and a set-up sniffer on a fake UART, with every state the sniffer publishes.
struct SnifferRig {
  FakeUART uart;
  TestableSECTouch hub;
  SecTouchSniffer sniffer{&hub};
  SecTouchSnifferScanSwitch scan_switch;
  std::vector<std::string> published;

  SnifferRig() {
    this->hub.set_uart_parent(&this->uart);
    this->scan_switch.set_parent(&this->sniffer);
    this->sniffer.set_scan_switch(&this->scan_switch);
    this->sniffer.add_on_state_callback([this](const std::string &state) { this->published.push_back(state); });
    this->sniffer.setup();
  }

  void loop_hub() {
    for (int i = 0; i < 3; i++) {
      this->hub.loop();
    }
  }

  void receive(int property_id, const std::string &value) {
    this->uart.push_rx(data_frame(property_id, value));
    this->loop_hub();
  }

  /// Runs the sniffer's timers due after `ms`. Host millis() is the real clock, so this waits.
  static void run_timers_after(uint32_t ms) {
    delay(ms);
    App.scheduler.call(millis());
  }

  /// A scan left running would leave its timers in the shared scheduler, pointing at this rig after it is gone.
  ~SnifferRig() { this->sniffer.stop_scan(); }

  /// Runs the sniffer's loop, as the main loop does once a request_scan() enabled it.
  void loop_sniffer() { this->sniffer.loop(); }

  /// Starts a scan of `run`. The hub's first idle loop schedules the first query.
  void start_scan(const ScanRun &run) {
    this->sniffer.start_scan(run);
    this->loop_hub();
  }

  /// Sends the scan query the sniffer scheduled `after_ms` from now.
  void send_scan_query(uint32_t after_ms) {
    run_timers_after(after_ms);
    this->loop_hub();
  }

  /// Sends the next scan query and answers it with `value`. The sniffer then schedules the query after it.
  void answer_scan_query(int property_id, const std::string &value) {
    this->send_scan_query(SecTouchSniffer::SCAN_NEXT_DELAY_MS);
    this->receive(property_id, value);
  }

  /// Lets the GET in flight run into the hub's watchdog, as if the panel never answered.
  void time_out_scan_query() {
    this->hub.task_start_time_ = millis() - (TASK_TIMEOUT_MS + 1);
    this->loop_hub();
  }

  /// Sends each scan query in turn and answers it, until the scan ends.
  void answer_every_scan_query() {
    for (int guard = 0; guard <= MAX_PROPERTY_ID && this->sniffer.is_scanning(); guard++) {
      const size_t before = this->gets_sent().size();
      this->send_scan_query(SecTouchSniffer::SCAN_NEXT_DELAY_MS);
      const std::vector<int> sent = this->gets_sent();
      ASSERT_GT(sent.size(), before) << "the scan sent no query";
      this->receive(sent.back(), "0");
    }
    ASSERT_FALSE(this->sniffer.is_scanning());
  }

  /// The property ID of every GET the hub sent, in order.
  std::vector<int> gets_sent() const {
    const std::string sent(this->uart.tx.begin(), this->uart.tx.end());
    const std::string get_head = std::string(1, static_cast<char>(STX)) + std::to_string(COMMANDID_GET) + tab;
    std::vector<int> ids;
    for (size_t at = sent.find(get_head); at != std::string::npos; at = sent.find(get_head, at + 1)) {
      ids.push_back(std::atoi(sent.c_str() + at + get_head.size()));
    }
    return ids;
  }

  /// How many GETs for `property_id` the hub sent.
  int gets_sent_for(int property_id) const {
    const std::string sent(this->uart.tx.begin(), this->uart.tx.end());
    const std::string get_head = std::string(1, static_cast<char>(STX)) + std::to_string(COMMANDID_GET) + tab +
                                 std::to_string(property_id) + tab;
    int count = 0;
    for (size_t at = sent.find(get_head); at != std::string::npos; at = sent.find(get_head, at + 1)) {
      count++;
    }
    return count;
  }
};

}  // namespace

TEST(SecTouchSnifferTest, PublishesAPropertyTheHubHasNotRegistered) {
  SnifferRig rig;

  rig.receive(UNREGISTERED_ID, "10");

  EXPECT_EQ(rig.sniffer.get_state(), UNREGISTERED_STATE);
}

TEST(SecTouchSnifferTest, ARepeatedIdenticalMessageIsNotPublishedAgain) {
  SnifferRig rig;
  rig.receive(UNREGISTERED_ID, "10");

  rig.receive(UNREGISTERED_ID, "10");

  EXPECT_EQ(rig.published, (std::vector<std::string>{UNREGISTERED_STATE}));
}

TEST(SecTouchSnifferTest, ARegisteredIdIsRecordedButNotListed) {
  SnifferRig rig;
  rig.hub.register_manual_update_listener(REGISTERED_ID, [](int, int) {});

  rig.receive(REGISTERED_ID, "2");
  rig.receive(UNREGISTERED_ID, "10");

  EXPECT_EQ(rig.published, (std::vector<std::string>{UNREGISTERED_STATE}));
}

// The state is published once, at the end, with everything the scan found; nothing is published while it runs.
TEST(SecTouchSnifferTest, ACompletedScanPublishesOnceWithEverythingItFound) {
  SnifferRig rig;
  rig.start_scan(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID));

  rig.answer_scan_query(UNREGISTERED_ID, "10");
  rig.answer_scan_query(NEXT_UNREGISTERED_ID, "11");

  EXPECT_FALSE(rig.sniffer.is_scanning());
  EXPECT_EQ(rig.published, (std::vector<std::string>{UNREGISTERED_STATE + STATE_SEPARATOR +
                                                     std::to_string(NEXT_UNREGISTERED_ID) + "=11"}));
}

TEST(SecTouchSnifferTest, StoppingAScanPublishesOnceWithWhatItFoundSoFar) {
  SnifferRig rig;
  rig.start_scan(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID));
  rig.answer_scan_query(UNREGISTERED_ID, "10");

  rig.sniffer.stop_scan();

  EXPECT_FALSE(rig.sniffer.is_scanning());
  EXPECT_EQ(rig.published, (std::vector<std::string>{UNREGISTERED_STATE}));
}

// Waits out SCAN_RETRY_DELAY_MS on the real clock.
TEST(SecTouchSnifferTest, ATimedOutScanQueryIsRetriedOnceThenSkipped) {
  SnifferRig rig;
  rig.start_scan(discover(UNREGISTERED_ID, UNREGISTERED_ID));
  rig.send_scan_query(SecTouchSniffer::SCAN_NEXT_DELAY_MS);
  rig.time_out_scan_query();
  ASSERT_TRUE(rig.sniffer.is_scanning());

  rig.send_scan_query(SecTouchSniffer::SCAN_RETRY_DELAY_MS);
  rig.time_out_scan_query();

  EXPECT_EQ(rig.gets_sent_for(UNREGISTERED_ID), 2);
  EXPECT_FALSE(rig.sniffer.is_scanning());
  // A record that only ever failed is not listed, so the end-of-scan state is empty.
  EXPECT_EQ(rig.published, (std::vector<std::string>{""}));
}

// --- Scan goals ---

TEST(SecTouchSnifferTest, ACheckScanQueriesExactlyTheKnownIdsInAscendingOrder) {
  SnifferRig rig;
  // Registered IDs are known, so check queries them too.
  rig.hub.register_manual_update_listener(REGISTERED_ID, [](int, int) {});
  rig.start_scan(CHECK_RUN);

  rig.answer_every_scan_query();

  EXPECT_FALSE(rig.sniffer.is_scanning());
  EXPECT_EQ(rig.gets_sent(), known_ids_ascending());
}

TEST(SecTouchSnifferTest, ADiscoverStartDuringACheckChangesNothing) {
  SnifferRig rig;
  rig.start_scan(CHECK_RUN);
  rig.answer_scan_query(known_ids_ascending().front(), "1");

  rig.sniffer.request_scan(true, discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID));
  rig.loop_sniffer();
  rig.answer_every_scan_query();

  EXPECT_EQ(rig.gets_sent(), known_ids_ascending());
}

TEST(SecTouchSnifferTest, ADiscoverScanSkipsKnownIds) {
  SnifferRig rig;
  rig.start_scan(discover(SUMMER_VENTILATION_PROPERTY_ID - 1, SUMMER_VENTILATION_PROPERTY_ID + 1));

  rig.answer_every_scan_query();

  EXPECT_EQ(rig.gets_sent(),
            (std::vector<int>{SUMMER_VENTILATION_PROPERTY_ID - 1, SUMMER_VENTILATION_PROPERTY_ID + 1}));
}

TEST(SecTouchSnifferTest, AStartAfterAStopBeginsAtTheFirstIdAgain) {
  SnifferRig rig;
  // Three unknown IDs, so the stop lands mid-run.
  const ScanRun run = discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID + 1);
  rig.start_scan(run);
  rig.answer_scan_query(UNREGISTERED_ID, "10");
  rig.sniffer.stop_scan();

  rig.start_scan(run);
  rig.answer_every_scan_query();

  EXPECT_EQ(rig.gets_sent(),
            (std::vector<int>{UNREGISTERED_ID, UNREGISTERED_ID, NEXT_UNREGISTERED_ID, NEXT_UNREGISTERED_ID + 1}));
}

// --- Scan start and stop from the page: queued by request_scan(), applied by loop() ---

TEST(SecTouchSnifferTest, ARequestedStartWaitsForTheLoop) {
  SnifferRig rig;

  rig.sniffer.request_scan(true, discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID));
  EXPECT_FALSE(rig.sniffer.is_scanning());

  rig.loop_sniffer();
  EXPECT_TRUE(rig.sniffer.is_scanning());
  EXPECT_TRUE(rig.scan_switch.state);
}

TEST(SecTouchSnifferTest, ARequestedStartScansTheRequestedRun) {
  SnifferRig rig;
  rig.sniffer.request_scan(true, discover(NEXT_UNREGISTERED_ID, NEXT_UNREGISTERED_ID));
  rig.loop_sniffer();
  rig.loop_hub();

  rig.answer_every_scan_query();

  EXPECT_EQ(rig.gets_sent(), std::vector<int>{NEXT_UNREGISTERED_ID});
}

TEST(SecTouchSnifferTest, ARequestedStopEndsTheScanAndMovesTheSwitch) {
  SnifferRig rig;
  rig.start_scan(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID));
  ASSERT_TRUE(rig.scan_switch.state);

  rig.sniffer.request_scan(false);
  rig.loop_sniffer();

  EXPECT_FALSE(rig.sniffer.is_scanning());
  EXPECT_FALSE(rig.scan_switch.state);
}

TEST(SecTouchSnifferTest, AStartDuringAScanChangesNothing) {
  SnifferRig rig;
  rig.start_scan(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID));
  rig.answer_scan_query(UNREGISTERED_ID, "10");

  rig.sniffer.request_scan(true, discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID));
  rig.loop_sniffer();

  EXPECT_TRUE(rig.sniffer.is_scanning());
  // Not restarted: the scan still finishes after NEXT_UNREGISTERED_ID.
  rig.answer_scan_query(NEXT_UNREGISTERED_ID, "11");
  EXPECT_FALSE(rig.sniffer.is_scanning());
  EXPECT_EQ(rig.gets_sent_for(UNREGISTERED_ID), 1);
}

TEST(SecTouchSnifferTest, AStopWhileIdleChangesNothing) {
  SnifferRig rig;

  rig.sniffer.request_scan(false);
  rig.loop_sniffer();

  EXPECT_FALSE(rig.sniffer.is_scanning());
  // Stopping an idle scan would publish the records; an ignored stop publishes nothing.
  EXPECT_TRUE(rig.published.empty());
}

TEST(SecTouchSnifferTest, ARequestIsAppliedOnce) {
  SnifferRig rig;
  rig.sniffer.request_scan(true, discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID));
  rig.loop_sniffer();
  rig.sniffer.stop_scan();

  rig.loop_sniffer();

  EXPECT_FALSE(rig.sniffer.is_scanning());
}

// No YAML range: the switch starts a discover of the default range through the same request path.
TEST(SecTouchSnifferTest, TheSwitchStartsADiscoverOfTheDefaultRange) {
  SnifferRig rig;

  rig.scan_switch.turn_on();
  EXPECT_FALSE(rig.sniffer.is_scanning());
  rig.loop_sniffer();
  rig.loop_hub();
  rig.send_scan_query(SecTouchSniffer::SCAN_NEXT_DELAY_MS);

  EXPECT_TRUE(rig.sniffer.is_scanning());
  EXPECT_TRUE(rig.scan_switch.state);
  EXPECT_EQ(rig.gets_sent(), std::vector<int>{first_scan_id(DEFAULT_DISCOVER_RUN)});

  rig.scan_switch.turn_off();
  rig.loop_sniffer();
  EXPECT_FALSE(rig.sniffer.is_scanning());
}

}  // namespace esphome::sec_touch::testing
