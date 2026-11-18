#include <gtest/gtest.h>

#include <string>

#include "../common/wire_message.h"
#include "sec_touch/message_crc.h"

using esphome::sec_touch::check_message_crc;
using esphome::sec_touch::CrcCheck;
using sec_touch_test::wire_message;

namespace {

// Index of the TAB before the CRC field: the third TAB.
int crc_tab(const std::string &msg) {
  size_t pos = 0;
  for (int i = 0; i < 3; ++i) {
    pos = msg.find('\t', i == 0 ? 0 : pos + 1);
  }
  return static_cast<int>(pos);
}

CrcCheck check(const std::string &body) {
  std::string msg = wire_message(body);
  return check_message_crc(msg.data(), static_cast<int>(msg.size()), crc_tab(msg));
}

}  // namespace

// Good messages captured on the installed controller, 2026-11-17 (issue #43).
TEST(MessageCrc, AcceptsCapturedReplyFor173) { EXPECT_EQ(check("32\t173\t10\t42696"), CrcCheck::CRC_MATCH); }
TEST(MessageCrc, AcceptsCapturedReplyFor176) { EXPECT_EQ(check("32\t176\t0\t58673"), CrcCheck::CRC_MATCH); }
TEST(MessageCrc, AcceptsCapturedReplyFor53) { EXPECT_EQ(check("32\t53\t10\t65173"), CrcCheck::CRC_MATCH); }
TEST(MessageCrc, AcceptsCapturedReplyFor202) { EXPECT_EQ(check("32\t202\t10\t7258"), CrcCheck::CRC_MATCH); }

// The reply for 173 with one bit flipped ('7' -> '3'): it arrived as property 133.
TEST(MessageCrc, RejectsCapturedCorruptedReply) { EXPECT_EQ(check("32\t133\t10\t42696"), CrcCheck::CRC_MISMATCH); }

TEST(MessageCrc, RejectsEmptyCrcField) { EXPECT_EQ(check("32\t173\t10\t"), CrcCheck::CRC_NOT_A_NUMBER); }
TEST(MessageCrc, RejectsNonDigitCrcField) { EXPECT_EQ(check("32\t173\t10\t42a96"), CrcCheck::CRC_NOT_A_NUMBER); }
TEST(MessageCrc, RejectsSignedCrcField) { EXPECT_EQ(check("32\t173\t10\t-4269"), CrcCheck::CRC_NOT_A_NUMBER); }
TEST(MessageCrc, RejectsCrcFieldAbove16Bits) {
  // 42696 + 65536: atoi-and-truncate would wrap this to a match.
  EXPECT_EQ(check("32\t173\t10\t108232"), CrcCheck::CRC_NOT_A_NUMBER);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
