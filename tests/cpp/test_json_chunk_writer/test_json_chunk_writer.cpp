#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "sec_touch_sniffer/json_chunk_writer.h"

using esphome::sec_touch::JSON_CHUNK_BYTES;
using esphome::sec_touch::JsonChunkWriter;

namespace {

/// Every chunk the writer sends, in order.
struct Chunks {
  std::vector<std::string> sent;
  /// The sink fails from this chunk on (counted from 0).
  size_t fail_from = SIZE_MAX;

  static bool sink(void *context, const char *data, size_t len) {
    auto *self = static_cast<Chunks *>(context);
    if (self->sent.size() >= self->fail_from) {
      return false;
    }
    self->sent.emplace_back(data, len);
    return true;
  }

  std::string joined() const {
    std::string all;
    for (const auto &chunk : this->sent) {
      all += chunk;
    }
    return all;
  }
};

/// Writes with `write`, flushes, and returns the whole output.
template<typename F> std::string written(F write) {
  Chunks chunks;
  JsonChunkWriter w(&Chunks::sink, &chunks);
  write(w);
  w.flush();
  return chunks.joined();
}

int hex_digit(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

/// Decodes one JSON string literal the way the page's JSON.parse does, mapping each code point below 0x100 back to
/// one byte (the export's Latin-1 step). Fails the test on anything else.
std::string decode_json_string(const std::string &json) {
  EXPECT_GE(json.size(), 2u);
  EXPECT_EQ(json.front(), '"');
  EXPECT_EQ(json.back(), '"');
  std::string out;
  for (size_t i = 1; i + 1 < json.size(); i++) {
    const unsigned char c = json[i];
    EXPECT_GE(c, 0x20) << "raw control byte in JSON at " << i;
    EXPECT_LT(c, 0x80) << "raw non-ASCII byte in JSON at " << i;
    EXPECT_NE(c, '"') << "unescaped quote at " << i;
    if (c != '\\') {
      out += static_cast<char>(c);
      continue;
    }
    const char kind = json[++i];
    if (kind == '"' || kind == '\\' || kind == '/') {
      out += kind;
      continue;
    }
    EXPECT_EQ(kind, 'u') << "unexpected escape \\" << kind;
    int code = 0;
    for (int d = 0; d < 4; d++) {
      code = code * 16 + hex_digit(json[++i]);
    }
    EXPECT_LT(code, 0x100);
    out += static_cast<char>(code);
  }
  return out;
}

}  // namespace

TEST(JsonChunkWriter, AsciiPassesUnchanged) {
  EXPECT_EQ(written([](JsonChunkWriter &w) { w.string("11.11.26 SWZ-0021E21H"); }), "\"11.11.26 SWZ-0021E21H\"");
}

TEST(JsonChunkWriter, QuoteAndBackslashGetTheirStandardEscapes) {
  EXPECT_EQ(written([](JsonChunkWriter &w) { w.string("a\"b\\c"); }), "\"a\\\"b\\\\c\"");
}

TEST(JsonChunkWriter, ControlAndNonAsciiBytesBecomeOneCodePointEach) {
  const std::string raw = std::string("\x01\t\n\x1f", 4) + "\x7f" + "\x80\xE2\xFF";
  EXPECT_EQ(written([&](JsonChunkWriter &w) { w.string(raw); }),
            "\"\\u0001\\u0009\\u000a\\u001f\x7f\\u0080\\u00e2\\u00ff\"");
}

TEST(JsonChunkWriter, EveryByteRoundTripsByteExact) {
  std::string raw;
  for (int b = 0; b < 256; b++) {
    raw += static_cast<char>(b);
  }
  EXPECT_EQ(decode_json_string(written([&](JsonChunkWriter &w) { w.string(raw); })), raw);
}

TEST(JsonChunkWriter, PaddingAndEmbeddedNulAreKept) {
  const std::string raw("00\0  ", 5);
  EXPECT_EQ(decode_json_string(written([&](JsonChunkWriter &w) { w.string(raw); })), raw);
}

TEST(JsonChunkWriter, WritesNumbersBooleansAndNull) {
  EXPECT_EQ(written([](JsonChunkWriter &w) {
              w.number(0);
              w.raw(",");
              w.number(4294967295000ull);
              w.raw(",");
              w.boolean(true);
              w.raw(",");
              w.boolean(false);
              w.raw(",");
              w.null();
            }),
            "0,4294967295000,true,false,null");
}

TEST(JsonChunkWriter, SecondsAreSentAsMilliseconds) {
  EXPECT_EQ(written([](JsonChunkWriter &w) { w.ms_or_null(true, 3); }), "3000");
}

TEST(JsonChunkWriter, TheLargestUptimeDoesNotOverflow) {
  EXPECT_EQ(written([](JsonChunkWriter &w) { w.ms_or_null(true, UINT32_MAX); }), "4294967295000");
}

TEST(JsonChunkWriter, ZeroSecondsIsATime) {
  EXPECT_EQ(written([](JsonChunkWriter &w) { w.ms_or_null(true, 0); }), "0");
}

TEST(JsonChunkWriter, AnUnsetTimeIsNull) {
  EXPECT_EQ(written([](JsonChunkWriter &w) { w.ms_or_null(false, 3); }), "null");
}

TEST(JsonChunkWriter, LongOutputIsSentInFullChunks) {
  Chunks chunks;
  JsonChunkWriter w(&Chunks::sink, &chunks);
  const std::string value(3 * JSON_CHUNK_BYTES, 'x');
  w.string(value);
  w.flush();

  ASSERT_EQ(chunks.sent.size(), 4u);
  for (size_t i = 0; i + 1 < chunks.sent.size(); i++) {
    EXPECT_EQ(chunks.sent[i].size(), JSON_CHUNK_BYTES);
  }
  EXPECT_EQ(chunks.joined(), "\"" + value + "\"");
}

TEST(JsonChunkWriter, AnEscapeAcrossAChunkBoundaryStaysWhole) {
  // Pushes a `\u00XX` escape across the end of the first chunk at every offset.
  for (size_t lead = JSON_CHUNK_BYTES - 8; lead < JSON_CHUNK_BYTES + 2; lead++) {
    Chunks chunks;
    JsonChunkWriter w(&Chunks::sink, &chunks);
    const std::string raw = std::string(lead, 'a') + "\x01\xFF\"";
    w.string(raw);
    w.flush();

    for (const auto &chunk : chunks.sent) {
      EXPECT_LE(chunk.size(), JSON_CHUNK_BYTES);
    }
    EXPECT_EQ(decode_json_string(chunks.joined()), raw) << "lead " << lead;
  }
}

TEST(JsonChunkWriter, NothingIsSentBeforeAChunkFillsOrFlush) {
  Chunks chunks;
  JsonChunkWriter w(&Chunks::sink, &chunks);
  w.raw("{}");
  EXPECT_TRUE(chunks.sent.empty());
  EXPECT_TRUE(w.flush());
  EXPECT_EQ(chunks.joined(), "{}");
}

TEST(JsonChunkWriter, FlushingAnEmptyBufferSendsNothing) {
  Chunks chunks;
  JsonChunkWriter w(&Chunks::sink, &chunks);
  EXPECT_TRUE(w.flush());
  EXPECT_TRUE(chunks.sent.empty());
}

TEST(JsonChunkWriter, AFailedSendStopsTheOutput) {
  Chunks chunks;
  chunks.fail_from = 1;
  JsonChunkWriter w(&Chunks::sink, &chunks);
  w.string(std::string(3 * JSON_CHUNK_BYTES, 'x'));

  EXPECT_FALSE(w.flush());
  EXPECT_FALSE(w.ok());
  EXPECT_EQ(chunks.sent.size(), 1u);
}
TEST(JsonChunkWriter, BeginStartsAFreshResponseOnAnotherContext) {
  Chunks first;
  first.fail_from = 0;
  Chunks second;
  JsonChunkWriter w(&Chunks::sink, &first);
  w.raw("lost");
  EXPECT_FALSE(w.flush());

  w.begin(&second);
  w.raw("[]");

  EXPECT_TRUE(w.flush());
  EXPECT_TRUE(first.sent.empty());
  EXPECT_EQ(second.joined(), "[]");
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
