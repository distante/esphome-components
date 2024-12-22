
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#include "_sec_touch_fan.h"

#ifndef SECONTROLLER_H
#define SECONTROLLER_H

#define STX 0x02
#define ETX 0x0A
#define ACK 0x06
#define TAB 0x09

#define RESET_ACK_MILLIS 8000

#define PROCESS_REQUESTREGISTER_DELAY_MILLIS 10
#define PROCESS_SENDBUFFER_DELAY_MILLIS 10
#define SEND_ACK_DELAY_MILLIS 2

#define COMMANDID_SET 32
#define COMMANDID_GET 32800

#define SECONTROLLER_BAUD 28800

#define ON_REGISTERCHANGED_MAX 10
#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif

namespace esphome {
namespace sec_touch {
class SECTouchComponent : public Component, public uart::UARTDevice {
#ifdef USE_BUTTON
  SUB_BUTTON(Poll)
#endif
 public:
  SECTouchComponent();
  void setup() override;
  void dump_config() override;
  void loop() override;
  void set_rx_pin(InternalGPIOPin *pin) { this->rx_pin_ = pin; }
  void set_tx_pin(InternalGPIOPin *pin) { this->tx_pin_ = pin; }

 public:
 protected:
  InternalGPIOPin *rx_pin_;
  InternalGPIOPin *tx_pin_;
  SecTouchFanManager fanManager;
};
}  // namespace sec_touch
}  // namespace esphome

#endif