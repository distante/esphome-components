#include <gtest/gtest.h>

#include "../sec_touch/common.h"
#include "esphome/components/sec_touch_sniffer/sec_touch_sniffer.h"

namespace esphome::sec_touch::testing {

TEST(SecTouchSnifferTest, PublishesAPropertyTheHubHasNotRegistered) {
  FakeUART uart;
  SECTouchComponent hub;
  hub.set_uart_parent(&uart);
  SecTouchSniffer sniffer(&hub);
  sniffer.setup();

  uart.push_rx(data_frame(133, "10"));
  for (int i = 0; i < 3; i++) {
    hub.loop();
  }

  EXPECT_EQ(sniffer.get_state(), "133=10");
}

}  // namespace esphome::sec_touch::testing
