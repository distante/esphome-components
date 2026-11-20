#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../sec_touch/common.h"
#include "esphome/components/sec_touch_sniffer/sec_touch_sniffer.h"

namespace esphome::sec_touch::testing {

namespace {

/// An ID no entity registers.
constexpr int UNREGISTERED_ID = 133;
/// An ID an entity registers: fan pair 1's level.
constexpr int REGISTERED_ID = FAN_LEVEL_IDS[0];

/// The text sensor state listing UNREGISTERED_ID with value "10".
const std::string UNREGISTERED_STATE = std::to_string(UNREGISTERED_ID) + "=10";

/// A hub and a set-up sniffer on a fake UART, with every state the sniffer publishes.
struct SnifferRig {
  FakeUART uart;
  SECTouchComponent hub;
  SecTouchSniffer sniffer{&hub};
  std::vector<std::string> published;

  SnifferRig() {
    this->hub.set_uart_parent(&this->uart);
    this->sniffer.add_on_state_callback([this](const std::string &state) { this->published.push_back(state); });
    this->sniffer.setup();
  }

  void receive(int property_id, const std::string &value) {
    this->uart.push_rx(data_frame(property_id, value));
    for (int i = 0; i < 3; i++) {
      this->hub.loop();
    }
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

}  // namespace esphome::sec_touch::testing
