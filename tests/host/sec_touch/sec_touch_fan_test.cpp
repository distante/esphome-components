#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "common.h"
#include "esphome/components/sec_touch/fan/sec_touch_fan.h"

namespace esphome::sec_touch::testing {

namespace {

/// The level property of the fan from issue #54 ('Dachboden').
constexpr int LEVEL_ID = 177;
constexpr int LABEL_ID = 81;

/// A Home Assistant fan call goes through the real SecTouchFan::control() and comes out as a SET on the bus.
class SecTouchFanTest : public ::testing::Test {
 protected:
  void SetUp() override {
    this->hub_.set_uart_parent(&this->uart_);
    this->hub_.set_inter_task_delay(0);
    this->fan_.setup();
  }

  /// Sends the first queued task to the panel.
  void send_next_task() { this->hub_.loop(); }

  FakeUART uart_;
  SECTouchComponent hub_;
  SecTouchFan fan_{&this->hub_, LEVEL_ID, LABEL_ID};
};

}  // namespace

// Issue #54: Home Assistant sent "State: OFF Speed: 1" to an off fan, and the panel turned it on at level 1.
TEST_F(SecTouchFanTest, OffWithASpeedWritesLevelZeroToAnOffFan) {
  this->fan_.make_call().set_state(false).set_speed(1).perform();
  this->send_next_task();

  EXPECT_EQ(this->uart_.tx, set_frame(LEVEL_ID, std::to_string(OFF_LEVEL)));
  EXPECT_FALSE(this->fan_.state);
}

TEST_F(SecTouchFanTest, OnWithASpeedWritesThatLevel) {
  this->fan_.make_call().set_state(true).set_speed(4).perform();
  this->send_next_task();

  EXPECT_EQ(this->uart_.tx, set_frame(LEVEL_ID, "4"));
  EXPECT_TRUE(this->fan_.state);
  EXPECT_EQ(this->fan_.speed, 4);
}

TEST_F(SecTouchFanTest, SplitBurstWritesItsLevelAndShowsTheSliderAtMax) {
  this->fan_.set_split_special_modes(true);
  this->fan_.state = true;
  this->fan_.speed = 3;

  this->fan_.make_call().set_preset_mode("Burst").perform();
  this->send_next_task();

  EXPECT_EQ(this->uart_.tx, set_frame(LEVEL_ID, std::to_string(BURST_LEVEL)));
  EXPECT_EQ(this->fan_.speed, MAX_NORMAL_LEVEL);
  EXPECT_EQ(std::string(this->fan_.get_preset_mode().c_str()), "Burst");
}

// Without waiting for the panel's echo, a plain speed puts a special preset back to Normal.
TEST_F(SecTouchFanTest, ASpeedAfterAPresetPutsThePresetBackToNormal) {
  this->fan_.state = true;
  this->fan_.speed = 3;
  this->fan_.make_call().set_preset_mode("Sleep").perform();

  this->fan_.make_call().set_speed(2).perform();

  EXPECT_EQ(std::string(this->fan_.get_preset_mode().c_str()), "Normal");
  EXPECT_EQ(this->fan_.speed, 2);
}

}  // namespace esphome::sec_touch::testing
