#include "sec_touch.h"
#include "XModemCRC.h"
#include "esphome/core/log.h"

namespace esphome {
namespace sec_touch {

static const char *const TAG = "sec-touch";

SECTouchComponent::SECTouchComponent() {}

void SECTouchComponent::setup() {
  ESP_LOGI(TAG, "SEC-Touch initializing.");

  this->fanManager.printAllFans();

  ESP_LOGI(TAG, " SEC-Touch setup complete.");
}

void SECTouchComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "SEC-Touch:");
  LOG_PIN("  RX Pin: ", this->rx_pin_);
  LOG_PIN("  TX Pin: ", this->tx_pin_);
}

void SECTouchComponent::loop() {}

//// FROM MANUEL's FILE

// const int SECTouchComponent::FAN_LEVEL_REGISTERS[SECTouchComponent::FAN_LEVEL_COUNT] = {173, 174, 175, 176, 177,
// 178};

// const int SECTouchComponent::LABEL_REGISTERS[SECTouchComponent::LABEL_COUNT] = {78, 79, 80, 81, 82, 83};

}  // namespace sec_touch
}  // namespace esphome