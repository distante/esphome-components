#include "sec_touch.h"
#include "XModemCRC.h"

namespace esphome
{
  namespace sec_touch
  {

    static const char *const TAG = "sec-touch";

    SECTouchComponent::SECTouchComponent()
    {
      SendMessageBuffer[0] = '\0';
      ReceiveMessageBuffer[0] = '\0';

      memset(FanLevelValues, 0, sizeof(FanLevelValues));
      memset(LabelValues, 0, sizeof(LabelValues));

      FanLevelRegisterIndex = 0;
      LabelRegisterIndex = 0;
      LastMessageAccepted = true;
      PreviousMillisProcessFanLevels = millis();
      PreviousMillisProcessLabels = millis() - LABEL_UPDATE_INTERVAL;
    }

    void SECTouchComponent::setup()
    {
      ESP_LOGI(TAG, " SEC-Touch setup complete.");
    }

    void SECTouchComponent::dump_config()
    {
      ESP_LOGCONFIG(TAG, "SEC-Touch:");
    }

    void SECTouchComponent::loop()
    {
      // TODO: probably the pool
      SECTouchComponent::Poll();
    }

    //// FROM MANUEL's FILE

    const int SECTouchComponent::FAN_LEVEL_REGISTERS[SECTouchComponent::FAN_LEVEL_COUNT] = {
        173, 174, 175, 176, 177, 178};

    const int SECTouchComponent::LABEL_REGISTERS[SECTouchComponent::LABEL_COUNT] = {
        78, 79, 80, 81, 82, 83};

    bool SECTouchComponent::IsSendBufferEmpty()
    {
      return SendMessageBuffer[0] == '\0';
    }

    void SECTouchComponent::SendMessageRequest(int commandId, int registerId)
    {
      if (!IsSendBufferEmpty())
        ESP_LOGD(TAG, "SendMessageRequest but message buffer != null");

      int len = snprintf(SendMessageBuffer, sizeof(SendMessageBuffer), "%c%d%c%d%c", STX, commandId, TAB, registerId, TAB);

      unsigned short crc = GetXModemCRC(SendMessageBuffer, len);
      len += snprintf(SendMessageBuffer + len, sizeof(SendMessageBuffer) - len, "%u%c", crc, ETX);
    }

    void SECTouchComponent::SendMessageResponse(int registerId, const char *content)
    {
      if (!IsSendBufferEmpty())
        ESP_LOGD(TAG, "SendMessageResponse but message buffer != null");

      int len = snprintf(SendMessageBuffer, sizeof(SendMessageBuffer), "%c%d%c%d%c%s%c", STX, COMMANDID_SET, TAB, registerId, TAB, content, TAB);

      unsigned short crc = GetXModemCRC(SendMessageBuffer, len);
      len += snprintf(SendMessageBuffer + len, sizeof(SendMessageBuffer) - len, "%u%c", crc, ETX);
    }

    int SECTouchComponent::getFanLevelRegisterIndex(int registerId)
    {
      for (int i = 0; i < FAN_LEVEL_COUNT; i++)
      {
        if (FAN_LEVEL_REGISTERS[i] == registerId)
        {
          return i;
        }
      }
      return -1;
    }

    int SECTouchComponent::getLabelRegisterIndex(int registerId)
    {
      for (int i = 0; i < LABEL_COUNT; i++)
      {
        if (LABEL_REGISTERS[i] == registerId)
        {
          return i;
        }
      }
      return -1;
    }

    void SECTouchComponent::ProcessMessageResponseIncome(int commandId, int registerId, const char *content)
    {
      int index = getFanLevelRegisterIndex(registerId);
      if (index >= 0)
      {
        if (strcmp(FanLevelValues[index], content) != 0)
        {
          ESP_LOGD(TAG, "Fan value register %d changed to %s", registerId, content);
          strncpy(FanLevelValues[index], content, sizeof(FanLevelValues[index]) - 1);
          FanLevelValues[index][sizeof(FanLevelValues[index]) - 1] = '\0';
          for (unsigned int i = 0; i < OnRegisterChangedCount; i++)
          {
            OnRegisterChanged[i](this, registerId, content);
          }
        }
        return;
      }

      index = getLabelRegisterIndex(registerId);
      if (index >= 0)
      {
        if (strcmp(LabelValues[index], content) != 0)
        {
          ESP_LOGD(TAG, "Label value register %d changed to %s", registerId, content);
          strncpy(LabelValues[index], content, sizeof(LabelValues[index]) - 1);
          LabelValues[index][sizeof(LabelValues[index]) - 1] = '\0';
          for (unsigned int i = 0; i < OnRegisterChangedCount; i++)
          {
            OnRegisterChanged[i](this, registerId, content);
          }
        }
      }
    }

    void SECTouchComponent::ProcessSendMessageAck()
    {
      if (SendMessageAck && millis() - PreviousSerialAvailable > SEND_ACK_DELAY_MILLIS)
      {
        SendMessageAck = false;
        uint8_t ack_message[] = {STX, ACK, ETX};
        this->write_array(ack_message, sizeof(ack_message));
      }
    }

    void SECTouchComponent::ProcessMessage(const char *message)
    {
      if (message[0] == ACK && message[1] == '\0')
      {
        LastMessageAccepted = true;
        PreviousMillisAckReceived = millis();
      }
      else
      {
        SendMessageAck = true;
        int commandId = 0, registerId = 0;
        char content[32] = {0};

        int numScanned = sscanf(message, "%d\t%d\t%31s", &commandId, &registerId, content);

        if (numScanned == 3)
        {
          ProcessMessageResponseIncome(commandId, registerId, content);
        }
        else
        {
          ESP_LOGD(TAG, "ProcessMessage: sscanf failed");
        }
      }
    }

    void SECTouchComponent::ProcessFanLevelRegisters()
    {
      if (LastMessageAccepted && IsSendBufferEmpty())
      {
        int registerId = FAN_LEVEL_REGISTERS[FanLevelRegisterIndex];
        SendMessageRequest(COMMANDID_GET, registerId);
        FanLevelRegisterIndex = (FanLevelRegisterIndex + 1) % FAN_LEVEL_COUNT;
        PreviousMillisProcessFanLevels = millis();
      }
    }

    void SECTouchComponent::ProcessLabelRegisters()
    {
      if (LastMessageAccepted && IsSendBufferEmpty())
      {
        int registerId = LABEL_REGISTERS[LabelRegisterIndex];
        SendMessageRequest(COMMANDID_GET, registerId);
        LabelRegisterIndex++;

        if (LabelRegisterIndex >= LABEL_COUNT)
        {
          LabelRegisterIndex = 0;
          PreviousMillisProcessLabels = millis();
        }
      }
    }

    void SECTouchComponent::ProcessMessageSendBuffer()
    {
      if (!SendMessageAck && !IsSendBufferEmpty())
      {
        // SECSerial->write((uint8_t *)SendMessageBuffer, strlen(SendMessageBuffer));
        this->write_array(reinterpret_cast<const uint8_t*>(SendMessageBuffer), strlen(SendMessageBuffer));
        SendMessageBuffer[0] = '\0';
        LastMessageAccepted = false;
      }
    }

    void SECTouchComponent::AddOnRegisterChanged(RegisterChangedCallback callback)
    {
      if (OnRegisterChangedCount < ON_REGISTERCHANGED_MAX)
      {
        OnRegisterChanged[OnRegisterChangedCount] = callback;
        OnRegisterChangedCount++;
      }
    }

    void SECTouchComponent::Poll()
    {
      
      // Check if there is data available to read from the serial interface
      if (this->available()) //SECSerial->available()
      {
        // Update the timestamp of the last serial data availability
        PreviousSerialAvailable = millis();
        // Read the incoming byte from the serial interface
        char incomingByte = this->read(); // SECSerial->read();

        // Check if the incoming byte is the start of text (STX) character
        if (incomingByte == STX)
        {
          // Set flags indicating that a message is being received
          InsideMessageFlag = true;
          ReceivedSTXFlag = true;
          // Clear the receive message buffer
          ReceiveMessageBuffer[0] = '\0';
        }
        // Check if the incoming byte is the end of text (ETX) character and a message is being received
        else if (incomingByte == ETX && InsideMessageFlag && ReceivedSTXFlag)
        {
          // Reset flags indicating that the message has been fully received
          InsideMessageFlag = false;
          ReceivedSTXFlag = false;
          // Process the received message
          ProcessMessage(ReceiveMessageBuffer);
        }
        // If a message is being received, append the incoming byte to the receive message buffer
        else if (InsideMessageFlag)
        {
          // Get the current length of the receive message buffer
          size_t len = strlen(ReceiveMessageBuffer);
          // Check if there is space left in the buffer
          if (len < sizeof(ReceiveMessageBuffer) - 1)
          {
            // Append the incoming byte to the buffer and null-terminate the string
            ReceiveMessageBuffer[len] = incomingByte;
            ReceiveMessageBuffer[len + 1] = '\0';
          }
        }
      }

      // Get the current time in milliseconds
      unsigned long currentMillis = millis();

      // Check if it's time to process fan level registers
      if (currentMillis - PreviousMillisProcessFanLevels > PROCESS_REQUESTREGISTER_DELAY_MILLIS)
      {
        // Process fan level registers
        ProcessFanLevelRegisters();
      }

      // Check if it's time to process label registers or if there are pending label registers to process
      if ((currentMillis - PreviousMillisProcessLabels >= LABEL_UPDATE_INTERVAL) ||
          (LabelRegisterIndex > 0 && LastMessageAccepted && IsSendBufferEmpty()))
      {
        // Process label registers
        ProcessLabelRegisters();
      }

      // Check if the acknowledgment timeout has been reached
      if (currentMillis - PreviousMillisAckReceived > RESET_ACK_MILLIS)
      {
        // Reset the acknowledgment received timestamp and mark the last message as accepted
        PreviousMillisAckReceived = currentMillis;
        LastMessageAccepted = true;
      }

      // Process sending of acknowledgment messages if needed
      ProcessSendMessageAck();

      // Check if the last message was accepted
      if (LastMessageAccepted)
      {
        // Check if it's time to process the send buffer
        if (currentMillis - PreviousSerialAvailable > PROCESS_SENDBUFFER_DELAY_MILLIS)
        {
          // Process the send buffer
          ProcessMessageSendBuffer();
        }
      }
    }

  }
}