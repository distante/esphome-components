#include "sec_touch.h"

namespace esphome
{
  namespace sec_touch
  {

    static const char *const TAG = "sec-touch";

    SECTouchComponent::SECTouchComponent() {}

    void SECTouchComponent::setup()
    {
      ESP_LOGCONFIG(TAG, " SEC-Touch setup complete.");
    }

    void SECTouchComponent::dump_config()
    {
      ESP_LOGCONFIG(TAG, "SEC-Touch:");

    }

    void SECTouchComponent::loop()
    {
      // TODO:
    }
  }
}