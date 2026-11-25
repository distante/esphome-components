#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "../sec_touch/common.h"
#include "esphome/components/sec_touch_sniffer/sec_touch_sniffer.h"
#include "esphome/components/sec_touch_sniffer/scan_goal.h"
#include "esphome/components/sec_touch_sniffer/write_probe.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"

namespace esphome::sec_touch::testing {

namespace {

/// An ID no entity registers, and unknown: the one after the time programs.
constexpr int UNREGISTERED_ID = TIME_PROGRAM_LEVEL_IDS.back() + 1;
/// An ID an entity registers: fan pair 1's level.
constexpr int REGISTERED_ID = FAN_LEVEL_IDS[0];

/// Another ID no entity registers, scanned right after UNREGISTERED_ID.
constexpr int NEXT_UNREGISTERED_ID = UNREGISTERED_ID + 1;
// Tests scan UNREGISTERED_ID up to NEXT_UNREGISTERED_ID + 1 as three unknown IDs in a row.
static_assert(!is_known_property(UNREGISTERED_ID) && !is_known_property(NEXT_UNREGISTERED_ID) &&
                  !is_known_property(NEXT_UNREGISTERED_ID + 1),
              "the unregistered test IDs must be unknown properties");

/// What the panel holds before a probe (probe_fixture.h's ORIGINAL).
const std::string PROBE_ORIGINAL = "01";
/// What a probe writes (probe_fixture.h's VALUE).
const std::string PROBE_VALUE = "03";

ScanRun discover(int from, int to) {
  return ScanRun{ScanGoal::SCAN_GOAL_DISCOVER, static_cast<uint8_t>(from), static_cast<uint8_t>(to)};
}

/// The catalogue's known IDs, ascending, straight from its arrays.
std::vector<int> known_ids_ascending() {
  std::vector<int> ids(FAN_LEVEL_IDS.begin(), FAN_LEVEL_IDS.end());
  ids.insert(ids.end(), FAN_LABEL_IDS.begin(), FAN_LABEL_IDS.end());
  ids.insert(ids.end(), GLOBAL_SETTING_IDS.begin(), GLOBAL_SETTING_IDS.end());
  ids.insert(ids.end(), TIME_PROGRAM_ENABLED_IDS.begin(), TIME_PROGRAM_ENABLED_IDS.end());
  ids.insert(ids.end(), TIME_PROGRAM_TIME_IDS.begin(), TIME_PROGRAM_TIME_IDS.end());
  ids.insert(ids.end(), TIME_PROGRAM_LEVEL_IDS.begin(), TIME_PROGRAM_LEVEL_IDS.end());
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
  ScanState scan() { return scan_state(this->scan_fields_()); }
  /// The scan object a stop request would get now, as the page handler builds it.
  ScanState stop_reply() { return scan_stop_reply(this->scan_fields_()); }
  /// loop()'s two halves, so a test can send a request between them.
  using SecTouchSniffer::Requests;
  Requests take_requests() { return this->take_requests_(); }
  void apply_requests(const Requests &requests) { this->apply_requests_(requests); }
  /// The probe object the page would get now; nullopt before any probe.
  std::optional<ProbeResult> probe() {
    ProbeResult result;
    if (!this->probe_result_(result)) {
      return std::nullopt;
    }
    return result;
  }
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

  /// Runs the sniffer's loop, as the main loop does once a request enabled it.
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

  rig.send_scan_query(SCAN_RETRY_DELAY_MS);
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

TEST(SecTouchSnifferTest, ADiscoverStartDuringACheckIsRefusedAndChangesNothing) {
  SnifferRig rig;
  rig.start_scan(CHECK_RUN);
  rig.answer_scan_query(known_ids_ascending().front(), "1");

  EXPECT_FALSE(rig.sniffer.request_scan_start(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID)));
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

// --- Scan start and stop from the page: queued by request_scan_start() and request_scan_stop(), applied by
// loop() ---

TEST(SecTouchSnifferTest, ARequestedStartWaitsForTheLoop) {
  SnifferRig rig;

  EXPECT_TRUE(rig.sniffer.request_scan_start(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID)));
  EXPECT_FALSE(rig.sniffer.is_scanning());

  rig.loop_sniffer();
  EXPECT_TRUE(rig.sniffer.is_scanning());
}

TEST(SecTouchSnifferTest, ARequestedStartScansTheRequestedRun) {
  SnifferRig rig;
  ASSERT_TRUE(rig.sniffer.request_scan_start(discover(NEXT_UNREGISTERED_ID, NEXT_UNREGISTERED_ID)));
  rig.loop_sniffer();
  rig.loop_hub();

  rig.answer_every_scan_query();

  EXPECT_EQ(rig.gets_sent(), std::vector<int>{NEXT_UNREGISTERED_ID});
}

TEST(SecTouchSnifferTest, ARequestedStopEndsTheScan) {
  SnifferRig rig;
  rig.start_scan(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID));
  ASSERT_TRUE(rig.sniffer.is_scanning());

  rig.sniffer.request_scan_stop();
  rig.loop_sniffer();

  EXPECT_FALSE(rig.sniffer.is_scanning());
}

TEST(SecTouchSnifferTest, AStartDuringAScanIsRefusedAndChangesNothing) {
  SnifferRig rig;
  rig.start_scan(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID));
  rig.answer_scan_query(UNREGISTERED_ID, "10");

  EXPECT_FALSE(rig.sniffer.request_scan_start(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID)));
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

  rig.sniffer.request_scan_stop();
  rig.loop_sniffer();

  EXPECT_FALSE(rig.sniffer.is_scanning());
  // Ending an idle scan again would mark the completed run stopped.
  EXPECT_TRUE(rig.sniffer.scan().done);
}

TEST(SecTouchSnifferTest, ARequestIsAppliedOnce) {
  SnifferRig rig;
  ASSERT_TRUE(rig.sniffer.request_scan_start(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID)));
  rig.loop_sniffer();
  rig.sniffer.stop_scan();

  rig.loop_sniffer();

  EXPECT_FALSE(rig.sniffer.is_scanning());
}

// --- One run at a time ---

// Each round races two tabs' starts through the real slot: never both accepted.
TEST(SecTouchSnifferTest, TwoSimultaneousStartsNeverBothSucceed) {
  for (int round = 0; round < 200; round++) {
    SnifferRig rig;
    std::atomic<bool> go{false};
    std::atomic<int> accepted{0};
    auto tab = [&](auto start) {
      while (!go) {
      }
      if (start()) {
        accepted++;
      }
    };
    std::thread scan_tab(tab,
                         [&]() { return rig.sniffer.request_scan_start(discover(UNREGISTERED_ID, UNREGISTERED_ID)); });
    std::thread probe_tab(tab, [&]() {
      return rig.sniffer.request_probe(ProbeKind::PROBE_KIND_PROBE, UNREGISTERED_ID, ProbeValue::of(PROBE_VALUE));
    });
    go = true;
    scan_tab.join();
    probe_tab.join();
    ASSERT_EQ(accepted, 1) << "round " << round;
  }
}

TEST(SecTouchSnifferTest, AStartWaitingForTheLoopHoldsTheSlot) {
  SnifferRig rig;
  ASSERT_TRUE(rig.sniffer.request_scan_start(discover(UNREGISTERED_ID, UNREGISTERED_ID)));

  EXPECT_FALSE(rig.sniffer.request_scan_start(CHECK_RUN));
  EXPECT_FALSE(
      rig.sniffer.request_probe(ProbeKind::PROBE_KIND_RESTORE, UNREGISTERED_ID, ProbeValue::of(PROBE_ORIGINAL)));
}

TEST(SecTouchSnifferTest, ARunsEndFreesTheSlot) {
  SnifferRig rig;
  rig.start_scan(discover(UNREGISTERED_ID, UNREGISTERED_ID));
  rig.answer_scan_query(UNREGISTERED_ID, "10");
  ASSERT_FALSE(rig.sniffer.is_scanning());

  EXPECT_TRUE(rig.sniffer.request_scan_start(CHECK_RUN));
}

TEST(SecTouchSnifferTest, AStopQueuedWithAStartStopsThatScanOnceItStarts) {
  SnifferRig rig;
  ASSERT_TRUE(rig.sniffer.request_scan_start(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID)));
  rig.sniffer.request_scan_stop();

  rig.loop_sniffer();

  EXPECT_FALSE(rig.sniffer.is_scanning());
  EXPECT_TRUE(rig.sniffer.request_scan_start(CHECK_RUN));
}

// The gap inside loop(): the queued start is taken, its scan not begun yet. A stop there still stops that scan.
TEST(SecTouchSnifferTest, AStopWhileLoopBeginsTheQueuedScanStopsIt) {
  SnifferRig rig;
  ASSERT_TRUE(rig.sniffer.request_scan_start(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID)));
  const TestableSniffer::Requests requests = rig.sniffer.take_requests();

  rig.sniffer.request_scan_stop();
  rig.sniffer.apply_requests(requests);
  rig.loop_sniffer();

  EXPECT_FALSE(rig.sniffer.is_scanning());
  EXPECT_TRUE(rig.sniffer.request_scan_start(CHECK_RUN));
}

// A stop queued while a scan runs belongs to that scan: once the scan completes, the next tab's start is untouched.
TEST(SecTouchSnifferTest, AStopForAScanThatEndedNeverStopsTheNextRun) {
  SnifferRig rig;
  rig.start_scan(discover(UNREGISTERED_ID, UNREGISTERED_ID));
  rig.sniffer.request_scan_stop();
  rig.answer_scan_query(UNREGISTERED_ID, "10");
  ASSERT_FALSE(rig.sniffer.is_scanning());

  ASSERT_TRUE(rig.sniffer.request_scan_start(discover(NEXT_UNREGISTERED_ID, NEXT_UNREGISTERED_ID)));
  rig.loop_sniffer();

  EXPECT_TRUE(rig.sniffer.is_scanning());
}

TEST(SecTouchSnifferTest, AStopWhileIdleQueuesNothing) {
  SnifferRig rig;
  rig.sniffer.request_scan_stop();
  ASSERT_TRUE(rig.sniffer.request_scan_start(discover(UNREGISTERED_ID, NEXT_UNREGISTERED_ID)));

  rig.loop_sniffer();

  EXPECT_TRUE(rig.sniffer.is_scanning());
}

// --- Narrowed scans ---

TEST(SecTouchSnifferTest, AListRunQueriesExactlyItsUnknownIdsInAscendingOrder) {
  SnifferRig rig;
  rig.start_scan(
      list_run(IdSet{NEXT_UNREGISTERED_ID, SUMMER_VENTILATION_PROPERTY_ID, UNREGISTERED_ID, NEXT_UNREGISTERED_ID}));

  rig.answer_every_scan_query();

  EXPECT_EQ(rig.gets_sent(), (std::vector<int>{UNREGISTERED_ID, NEXT_UNREGISTERED_ID}));
  EXPECT_TRUE(rig.sniffer.scan().done);
  EXPECT_TRUE(rig.sniffer.scan().listed);
}

// --- Write probes ---

namespace {

/// A rig whose hub sends each task at once, for a probe's back-to-back steps.
struct ProbeRig : SnifferRig {
  ProbeRig() { this->hub.set_inter_task_delay(0); }

  /// Starts a probe or restore from the page and lets the hub send its first request.
  void start(ProbeKind kind, const std::string &value) {
    ASSERT_TRUE(this->sniffer.request_probe(kind, UNREGISTERED_ID, ProbeValue::of(value)));
    this->loop_sniffer();
    this->loop_hub();
  }

  /// The panel takes the SET in flight.
  void ack() {
    this->uart.push_rx(ack_frame());
    this->loop_hub();
  }

  /// What the hub sent since the last call.
  std::vector<uint8_t> take_sent() {
    std::vector<uint8_t> sent = this->uart.tx;
    this->uart.tx.clear();
    return sent;
  }

  ~ProbeRig() { this->hub.exit_scan_mode(); }
};

std::vector<uint8_t> concat(std::vector<uint8_t> a, const std::vector<uint8_t> &b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

}  // namespace

// The whole sequence on the wire: GET the original, SET the value, read it back, restore, verify.
TEST(SecTouchSnifferTest, AWritableProbeRunsOnTheWireAndRestores) {
  ProbeRig rig;
  rig.start(ProbeKind::PROBE_KIND_PROBE, PROBE_VALUE);
  EXPECT_EQ(rig.take_sent(), get_frame(UNREGISTERED_ID));

  rig.receive(UNREGISTERED_ID, PROBE_ORIGINAL);
  // The hub ACKs the data frame, then sends the SET.
  EXPECT_EQ(rig.take_sent(), concat(ack_frame(), set_frame(UNREGISTERED_ID, PROBE_VALUE)));

  rig.ack();
  EXPECT_EQ(rig.take_sent(), get_frame(UNREGISTERED_ID));

  rig.receive(UNREGISTERED_ID, PROBE_VALUE);
  EXPECT_EQ(rig.take_sent(), concat(ack_frame(), set_frame(UNREGISTERED_ID, PROBE_ORIGINAL)));

  rig.ack();
  EXPECT_EQ(rig.take_sent(), get_frame(UNREGISTERED_ID));
  rig.receive(UNREGISTERED_ID, PROBE_ORIGINAL);

  const std::optional<ProbeResult> probe = rig.sniffer.probe();
  ASSERT_TRUE(probe.has_value());
  EXPECT_FALSE(probe->active);
  EXPECT_EQ(probe->verdict, ProbeVerdict::PROBE_VERDICT_WRITABLE);
  EXPECT_EQ(probe->restore, ProbeRestore::PROBE_RESTORE_OK);
  EXPECT_EQ(probe->original.view(), PROBE_ORIGINAL);
  EXPECT_EQ(probe->step_count, 5);
  // The probe's GETs pass the tap like any data message.
  EXPECT_EQ(rig.sniffer.store().raw_value(UNREGISTERED_ID), PROBE_ORIGINAL);
  EXPECT_FALSE(rig.sniffer.is_probing());
}

TEST(SecTouchSnifferTest, AProbePausesPollingUntilItEnds) {
  ProbeRig rig;
  rig.start(ProbeKind::PROBE_KIND_PROBE, PROBE_VALUE);
  rig.take_sent();
  // While the probe runs, a poll cycle sends nothing of its own.
  rig.hub.update();
  rig.loop_hub();
  EXPECT_TRUE(rig.take_sent().empty());

  // The original equals the value: nothing written, and the probe ends.
  rig.receive(UNREGISTERED_ID, PROBE_VALUE);

  EXPECT_FALSE(rig.sniffer.is_probing());
  EXPECT_EQ(rig.sniffer.probe()->verdict, ProbeVerdict::PROBE_VERDICT_NOTHING_WRITTEN);
  EXPECT_TRUE(rig.sniffer.request_scan_start(CHECK_RUN));
}

TEST(SecTouchSnifferTest, AStartOrStopDuringAProbeChangesNothing) {
  ProbeRig rig;
  rig.start(ProbeKind::PROBE_KIND_PROBE, PROBE_VALUE);

  EXPECT_FALSE(rig.sniffer.request_scan_start(CHECK_RUN));
  EXPECT_FALSE(
      rig.sniffer.request_probe(ProbeKind::PROBE_KIND_RESTORE, UNREGISTERED_ID, ProbeValue::of(PROBE_ORIGINAL)));
  rig.sniffer.request_scan_stop();
  rig.loop_sniffer();

  EXPECT_TRUE(rig.sniffer.is_probing());
  EXPECT_TRUE(rig.sniffer.probe()->active);
}

TEST(SecTouchSnifferTest, ARestoreRequestSetsTheOriginalAndVerifiesIt) {
  ProbeRig rig;
  rig.start(ProbeKind::PROBE_KIND_RESTORE, PROBE_ORIGINAL);
  EXPECT_EQ(rig.take_sent(), set_frame(UNREGISTERED_ID, PROBE_ORIGINAL));

  rig.ack();
  rig.receive(UNREGISTERED_ID, PROBE_ORIGINAL);

  EXPECT_EQ(rig.sniffer.probe()->kind, ProbeKind::PROBE_KIND_RESTORE);
  EXPECT_EQ(rig.sniffer.probe()->restore, ProbeRestore::PROBE_RESTORE_OK);
  EXPECT_FALSE(rig.sniffer.is_probing());
}

TEST(SecTouchSnifferTest, BeforeAnyProbeThereIsNoProbeResult) {
  SnifferRig rig;
  EXPECT_FALSE(rig.sniffer.probe().has_value());
}

}  // namespace esphome::sec_touch::testing
