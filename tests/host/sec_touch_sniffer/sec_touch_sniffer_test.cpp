#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "../sec_touch/common.h"
#include "esphome/components/sec_touch_sniffer/sec_touch_sniffer.h"
#include "esphome/components/sec_touch_sniffer/scan_goal.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"

namespace esphome::sec_touch::testing {

namespace {

/// An ID no entity registers.
constexpr int UNREGISTERED_ID = 133;
/// An ID an entity registers: fan pair 1's level.
constexpr int REGISTERED_ID = FAN_LEVEL_IDS[0];

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

/// The sniffer, with the records and the scan object its page handler serves.
class TestableSniffer : public SecTouchSniffer {
 public:
  using SecTouchSniffer::SecTouchSniffer;
  const PropertyStore &store() const { return this->store_; }
  /// The previous raw values of `id`, newest first, as the page gets them.
  std::vector<std::string> previous_values(int id) const {
    std::vector<std::string> values;
    this->store_.history().for_each_newest_first(
        static_cast<uint8_t>(id), [&values](uint32_t, std::string_view raw) { values.emplace_back(raw); });
    return values;
  }
  /// The scan object the page would get now.
  ScanState scan() const { return scan_state(this->scan_fields_()); }
  /// The scan object a stop request would get now, as the page handler builds it.
  ScanState stop_reply() const { return scan_reply(this->scan_fields_(), /*start=*/false, DEFAULT_DISCOVER_RUN); }
};

/// A hub and a set-up sniffer on a fake UART.
struct SnifferRig {
  FakeUART uart;
  TestableSECTouch hub;
  TestableSniffer sniffer{&hub};

  SnifferRig() {
    this->hub.set_uart_parent(&this->uart);
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
    std::vector<int> ids;
    for (size_t at = sent.find(get_start); at != std::string::npos; at = sent.find(get_start, at + 1)) {
      ids.push_back(std::atoi(sent.c_str() + at + get_start.size()));
    }
    return ids;
  }

  /// How many GETs for `property_id` the hub sent.
  int gets_sent_for(int property_id) const {
    const std::string sent(this->uart.tx.begin(), this->uart.tx.end());
    const std::vector<uint8_t> get = get_frame(property_id);
    const std::string get_bytes(get.begin(), get.end());
    int count = 0;
    for (size_t at = sent.find(get_bytes); at != std::string::npos; at = sent.find(get_bytes, at + 1)) {
      count++;
    }
    return count;
  }
};

}  // namespace

TEST(SecTouchSnifferTest, RecordsAPropertyTheHubSends) {
  SnifferRig rig;

  rig.receive(UNREGISTERED_ID, "10");

  EXPECT_EQ(rig.sniffer.store().raw_value(UNREGISTERED_ID), "10");
}

TEST(SecTouchSnifferTest, RecordsARegisteredPropertyTheHubSends) {
  SnifferRig rig;
  rig.hub.register_manual_update_listener(REGISTERED_ID, [](int, int) {});
  ASSERT_TRUE(rig.hub.is_property_registered(REGISTERED_ID));

  rig.receive(REGISTERED_ID, "3");

  EXPECT_EQ(rig.sniffer.store().raw_value(REGISTERED_ID), "3");
}

// Always on: the page is always compiled in, and its records list each property's previous values.
TEST(SecTouchSnifferTest, KeepsAHistoryOfPreviousRawValues) {
  SnifferRig rig;
  rig.receive(UNREGISTERED_ID, "10");

  rig.receive(UNREGISTERED_ID, "11");

  EXPECT_EQ(rig.sniffer.previous_values(UNREGISTERED_ID), std::vector<std::string>{"10"});
}

TEST(SecTouchSnifferTest, ACompletedScanRecordsEverythingItFound) {
  SnifferRig rig;
  rig.start_scan(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID));

  rig.answer_scan_query(UNREGISTERED_ID, "10");
  rig.answer_scan_query(NEXT_UNREGISTERED_ID, "11");

  EXPECT_FALSE(rig.sniffer.is_scanning());
  EXPECT_TRUE(rig.sniffer.scan().done);
  EXPECT_EQ(rig.sniffer.store().raw_value(UNREGISTERED_ID), "10");
  EXPECT_EQ(rig.sniffer.store().raw_value(NEXT_UNREGISTERED_ID), "11");
}

TEST(SecTouchSnifferTest, StoppingAScanEndsItNotDone) {
  SnifferRig rig;
  rig.start_scan(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID));
  rig.answer_scan_query(UNREGISTERED_ID, "10");

  rig.sniffer.stop_scan();

  EXPECT_FALSE(rig.sniffer.is_scanning());
  EXPECT_FALSE(rig.sniffer.scan().done);
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
  EXPECT_EQ(rig.sniffer.store().fields(UNREGISTERED_ID).query_status, ScanOutcome::SCAN_OUTCOME_TIMEOUT);
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

TEST(SecTouchSnifferTest, AStartAfterACompletedRunIsNotDone) {
  SnifferRig rig;
  const ScanRun run = discover(UNREGISTERED_ID, UNREGISTERED_ID);
  rig.start_scan(run);
  rig.answer_scan_query(UNREGISTERED_ID, "10");
  ASSERT_TRUE(rig.sniffer.scan().done);

  rig.start_scan(run);

  EXPECT_FALSE(rig.sniffer.scan().done);
}

TEST(SecTouchSnifferTest, AStopReplyMidRunAfterACompletedRunIsNotDone) {
  SnifferRig rig;
  rig.start_scan(discover(UNREGISTERED_ID, UNREGISTERED_ID));
  rig.answer_scan_query(UNREGISTERED_ID, "10");
  ASSERT_TRUE(rig.sniffer.scan().done);

  rig.start_scan(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID));
  const ScanState reply = rig.sniffer.stop_reply();

  EXPECT_FALSE(reply.done);
  EXPECT_EQ(reply.next, UNREGISTERED_ID);
}

TEST(SecTouchSnifferTest, DuringTheGapBeforeAQueryNextNamesThatId) {
  SnifferRig rig;
  rig.start_scan(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID + 1));
  EXPECT_EQ(rig.sniffer.scan().next, UNREGISTERED_ID);

  rig.answer_scan_query(UNREGISTERED_ID, "10");

  EXPECT_EQ(rig.sniffer.scan().next, NEXT_UNREGISTERED_ID);
}

TEST(SecTouchSnifferTest, AStopDuringTheGapReportsTheIdItNeverQueried) {
  SnifferRig rig;
  rig.start_scan(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID + 1));
  rig.answer_scan_query(UNREGISTERED_ID, "10");

  rig.sniffer.stop_scan();

  EXPECT_EQ(rig.gets_sent(), std::vector<int>{UNREGISTERED_ID});
  EXPECT_EQ(rig.sniffer.scan().next, NEXT_UNREGISTERED_ID);
}

// --- Scan start and stop from the page: queued by request_scan(), applied by loop() ---

TEST(SecTouchSnifferTest, ARequestedStartWaitsForTheLoop) {
  SnifferRig rig;

  rig.sniffer.request_scan(true, discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID));
  EXPECT_FALSE(rig.sniffer.is_scanning());

  rig.loop_sniffer();
  EXPECT_TRUE(rig.sniffer.is_scanning());
}

TEST(SecTouchSnifferTest, ARequestedStartScansTheRequestedRun) {
  SnifferRig rig;
  rig.sniffer.request_scan(true, discover(NEXT_UNREGISTERED_ID, NEXT_UNREGISTERED_ID));
  rig.loop_sniffer();
  rig.loop_hub();

  rig.answer_every_scan_query();

  EXPECT_EQ(rig.gets_sent(), std::vector<int>{NEXT_UNREGISTERED_ID});
}

TEST(SecTouchSnifferTest, ARequestedStopEndsTheScan) {
  SnifferRig rig;
  rig.start_scan(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID));
  ASSERT_TRUE(rig.sniffer.is_scanning());

  rig.sniffer.request_scan(false);
  rig.loop_sniffer();

  EXPECT_FALSE(rig.sniffer.is_scanning());
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
  rig.start_scan(discover(UNREGISTERED_ID, UNREGISTERED_ID));
  rig.answer_scan_query(UNREGISTERED_ID, "10");
  ASSERT_TRUE(rig.sniffer.scan().done);

  rig.sniffer.request_scan(false);
  rig.loop_sniffer();

  EXPECT_FALSE(rig.sniffer.is_scanning());
  // Ending an idle scan again would mark the completed run stopped.
  EXPECT_TRUE(rig.sniffer.scan().done);
}

TEST(SecTouchSnifferTest, ARequestIsAppliedOnce) {
  SnifferRig rig;
  rig.sniffer.request_scan(true, discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID));
  rig.loop_sniffer();
  rig.sniffer.stop_scan();

  rig.loop_sniffer();

  EXPECT_FALSE(rig.sniffer.is_scanning());
}

}  // namespace esphome::sec_touch::testing
