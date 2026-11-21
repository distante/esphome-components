#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../sec_touch/common.h"
#include "esphome/components/sec_touch_sniffer/sec_touch_sniffer.h"
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

/// A hub and a set-up sniffer on a fake UART, with every state the sniffer publishes.
struct SnifferRig {
  FakeUART uart;
  TestableSECTouch hub;
  SecTouchSniffer sniffer{&hub};
  std::vector<std::string> published;

  SnifferRig() {
    this->hub.set_uart_parent(&this->uart);
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

  /// Starts a scan of `first`..`last`. The hub's first idle loop schedules the first query.
  void start_scan(int first, int last) {
    this->sniffer.set_scan_range(first, last);
    this->sniffer.toggle_scan();
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
  rig.start_scan(UNREGISTERED_ID, NEXT_UNREGISTERED_ID);

  rig.answer_scan_query(UNREGISTERED_ID, "10");
  rig.answer_scan_query(NEXT_UNREGISTERED_ID, "11");

  EXPECT_FALSE(rig.sniffer.is_scanning());
  EXPECT_EQ(rig.published, (std::vector<std::string>{UNREGISTERED_STATE + STATE_SEPARATOR +
                                                     std::to_string(NEXT_UNREGISTERED_ID) + "=11"}));
}

TEST(SecTouchSnifferTest, StoppingAScanPublishesOnceWithWhatItFoundSoFar) {
  SnifferRig rig;
  rig.start_scan(UNREGISTERED_ID, NEXT_UNREGISTERED_ID);
  rig.answer_scan_query(UNREGISTERED_ID, "10");

  rig.sniffer.toggle_scan();

  EXPECT_FALSE(rig.sniffer.is_scanning());
  EXPECT_EQ(rig.published, (std::vector<std::string>{UNREGISTERED_STATE}));
}

// Waits out SCAN_RETRY_DELAY_MS on the real clock.
TEST(SecTouchSnifferTest, ATimedOutScanQueryIsRetriedOnceThenSkipped) {
  SnifferRig rig;
  rig.start_scan(UNREGISTERED_ID, UNREGISTERED_ID);
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

}  // namespace esphome::sec_touch::testing
