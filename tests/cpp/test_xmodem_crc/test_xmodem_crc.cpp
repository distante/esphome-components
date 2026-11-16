#include <gtest/gtest.h>

#include <cstring>

#include "sec_touch/XModemCRC.h"

// Known answers for CRC-16/XMODEM (poly 0x1021, init 0x0000), the standard check value.
TEST(XModemCRC, ChecksTheStandardCheckString) {
  const char *check = "123456789";

  EXPECT_EQ(GetXModemCRC(check, std::strlen(check)), 0x31C3);
}

TEST(XModemCRC, EmptyBufferIsZero) { EXPECT_EQ(GetXModemCRC("", 0), 0x0000); }

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
