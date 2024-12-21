/**
 * Modified version of https://github.com/Manuel-Siekmann/VentilationSystem
 */
#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"

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


#define ON_REGISTERCHANGED_MAX 10


namespace esphome
{
  namespace sec_touch
  {
    class SECTouchComponent : public Component, public uart::UARTDevice
    {
      public:
        SECTouchComponent();
        void setup() override;
        void dump_config() override;
        void loop() override;

      
    };
  }
}

#endif