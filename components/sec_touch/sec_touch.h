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

    protected:
      bool InsideMessageFlag = false;
      bool ReceivedSTXFlag = false;

      bool LastMessageAccepted = true;
      bool SendMessageAck = false;

      unsigned long PreviousMillisProcessFanLevels = 0;
      unsigned long PreviousMillisProcessLabels = 0;
      unsigned long PreviousMillisAckReceived = 0;
      unsigned long PreviousSerialAvailable = 0;

      const unsigned long LABEL_UPDATE_INTERVAL = 600000; // 10 Minuten

      int FanLevelRegisterIndex = 0;
      int LabelRegisterIndex = 0;

      typedef std::function<void(SECTouchComponent *, int, const char *)> RegisterChangedCallback;
      RegisterChangedCallback OnRegisterChanged[ON_REGISTERCHANGED_MAX];
      unsigned int OnRegisterChangedCount = 0;

      static const int FAN_LEVEL_COUNT = 6;
      static const int FAN_LEVEL_REGISTERS[FAN_LEVEL_COUNT];

      static const int LABEL_COUNT = 6;
      static const int LABEL_REGISTERS[LABEL_COUNT];

      char FanLevelValues[FAN_LEVEL_COUNT][16];
      char LabelValues[LABEL_COUNT][16];

      char SendMessageBuffer[64];
      char ReceiveMessageBuffer[64];


      bool IsSendBufferEmpty();
      void SendMessageRequest(int commandId, int registerId);
      void ProcessMessageResponseIncome(int commandId, int registerId, const char *content);
      void ProcessSendMessageAck();
      void ProcessMessage(const char *message);
      void ProcessFanLevelRegisters();
      void ProcessLabelRegisters();
      void ProcessMessageSendBuffer();
      void SendMessageResponse(int registerId, const char* content);
      void AddOnRegisterChanged(RegisterChangedCallback callback);
      void Poll(); //will be probably deleted

      int getFanLevelRegisterIndex(int registerId);
      int getLabelRegisterIndex(int registerId);
    };
  }
}

#endif