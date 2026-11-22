#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>

#include "sec_touch/known_property_meaning.h"

using esphome::sec_touch::AUTOMATIC_CO2_LEVEL;
using esphome::sec_touch::AUTOMATIC_CO2_NAME;
using esphome::sec_touch::BURST_LEVEL;
using esphome::sec_touch::BURST_NAME;
using esphome::sec_touch::CLOCK_PROPERTY_ID;
using esphome::sec_touch::DATE_PROPERTY_ID;
using esphome::sec_touch::FAN_LABEL_IDS;
using esphome::sec_touch::FAN_LEVEL_IDS;
using esphome::sec_touch::is_known_property;
using esphome::sec_touch::known_property_meaning;
using esphome::sec_touch::KnownMeaning;
using esphome::sec_touch::MAX_LEVEL;
using esphome::sec_touch::MAX_NORMAL_LEVEL;
using esphome::sec_touch::MeaningKind;
using esphome::sec_touch::MIN_NORMAL_LEVEL;
using esphome::sec_touch::NAME_MAPPING;
using esphome::sec_touch::NAME_MAPPING_COUNT;
using esphome::sec_touch::NOT_CONNECTED_LEVEL;
using esphome::sec_touch::NOT_CONNECTED_NAME;
using esphome::sec_touch::OFF_LEVEL;
using esphome::sec_touch::OFF_NAME;
using esphome::sec_touch::SLEEP_LEVEL;
using esphome::sec_touch::SLEEP_NAME;
using esphome::sec_touch::SUMMER_VENTILATION_OFF;
using esphome::sec_touch::SUMMER_VENTILATION_ON;
using esphome::sec_touch::SUMMER_VENTILATION_PROPERTY_ID;

namespace {

const char *kind_name(MeaningKind kind) {
  switch (kind) {
    case MeaningKind::NAME:
      return "NAME";
    case MeaningKind::AS_SENT:
      return "AS_SENT";
    case MeaningKind::UNEXPECTED:
      return "UNEXPECTED";
  }
  return "?";
}

::testing::AssertionResult means(const std::optional<KnownMeaning> &actual, MeaningKind kind, std::string_view text) {
  if (!actual.has_value()) {
    return ::testing::AssertionFailure() << "got nullopt, expected " << kind_name(kind) << " \"" << text << "\"";
  }
  if (actual->kind != kind || actual->text != text) {
    return ::testing::AssertionFailure() << "got " << kind_name(actual->kind) << " \"" << actual->text
                                         << "\", expected " << kind_name(kind) << " \"" << text << "\"";
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult means_name(const std::optional<KnownMeaning> &actual, std::string_view name) {
  return means(actual, MeaningKind::NAME, name);
}

::testing::AssertionResult is_unexpected(const std::optional<KnownMeaning> &actual, std::string_view trimmed) {
  return means(actual, MeaningKind::UNEXPECTED, trimmed);
}

const int FAN_1_LEVEL_ID = FAN_LEVEL_IDS.front();
const int FAN_6_LEVEL_ID = FAN_LEVEL_IDS.back();
const int FAN_1_LABEL_ID = FAN_LABEL_IDS.front();
const int FAN_6_LABEL_ID = FAN_LABEL_IDS.back();

}  // namespace

// ── known IDs ──

TEST(IsKnownProperty, KnowsEveryFanLevelAndLabelId) {
  for (int id : FAN_LEVEL_IDS) {
    EXPECT_TRUE(is_known_property(id)) << id;
  }
  for (int id : FAN_LABEL_IDS) {
    EXPECT_TRUE(is_known_property(id)) << id;
  }
}

TEST(IsKnownProperty, KnowsSummerVentilationDateAndClock) {
  EXPECT_TRUE(is_known_property(SUMMER_VENTILATION_PROPERTY_ID));
  EXPECT_TRUE(is_known_property(DATE_PROPERTY_ID));
  EXPECT_TRUE(is_known_property(CLOCK_PROPERTY_ID));
}

TEST(IsKnownProperty, DoesNotKnowTheIdsAroundThem) {
  EXPECT_FALSE(is_known_property(0));
  EXPECT_FALSE(is_known_property(FAN_1_LEVEL_ID - 1));
  EXPECT_FALSE(is_known_property(FAN_6_LEVEL_ID + 1));
  EXPECT_FALSE(is_known_property(FAN_1_LABEL_ID - 1));
  EXPECT_FALSE(is_known_property(FAN_6_LABEL_ID + 1));
  EXPECT_FALSE(is_known_property(SUMMER_VENTILATION_PROPERTY_ID + 1));
  EXPECT_FALSE(is_known_property(255));
}

TEST(KnownPropertyMeaning, IsNulloptForAnUnknownId) {
  EXPECT_FALSE(known_property_meaning(FAN_6_LEVEL_ID + 1, "3").has_value());
  EXPECT_FALSE(known_property_meaning(0, "").has_value());
}

// ── fan levels ──

TEST(KnownPropertyMeaning, NamesTheOffAndNormalLevels) {
  EXPECT_TRUE(means_name(known_property_meaning(FAN_1_LEVEL_ID, std::to_string(OFF_LEVEL)), OFF_NAME));
  EXPECT_TRUE(means_name(known_property_meaning(FAN_1_LEVEL_ID, std::to_string(MIN_NORMAL_LEVEL)), "level 1"));
  EXPECT_TRUE(means_name(known_property_meaning(FAN_1_LEVEL_ID, "3"), "level 3"));
  EXPECT_TRUE(means_name(known_property_meaning(FAN_6_LEVEL_ID, std::to_string(MAX_NORMAL_LEVEL)), "level 6"));
}

TEST(KnownPropertyMeaning, NamesTheSpecialLevelsWithTheirPresetNames) {
  EXPECT_TRUE(means_name(known_property_meaning(FAN_1_LEVEL_ID, std::to_string(BURST_LEVEL)), BURST_NAME));
  EXPECT_TRUE(
      means_name(known_property_meaning(FAN_1_LEVEL_ID, std::to_string(AUTOMATIC_CO2_LEVEL)), AUTOMATIC_CO2_NAME));
  EXPECT_TRUE(means_name(known_property_meaning(FAN_1_LEVEL_ID, std::to_string(SLEEP_LEVEL)), SLEEP_NAME));
}

TEST(KnownPropertyMeaning, NamesTheNotConnectedLevel) {
  EXPECT_TRUE(
      means_name(known_property_meaning(FAN_6_LEVEL_ID, std::to_string(NOT_CONNECTED_LEVEL)), NOT_CONNECTED_NAME));
}

// The panel pads values on the wire ("00               ").
TEST(KnownPropertyMeaning, ReadsALevelAsTheWholeTrimmedDecimalWithLeadingZeros) {
  EXPECT_TRUE(means_name(known_property_meaning(FAN_1_LEVEL_ID, "03"), "level 3"));
  EXPECT_TRUE(means_name(known_property_meaning(FAN_1_LEVEL_ID, "00               "), OFF_NAME));
  EXPECT_TRUE(means_name(known_property_meaning(FAN_1_LEVEL_ID, " \t7\r\n"), BURST_NAME));
}

TEST(KnownPropertyMeaning, ALevelOutsideTheTableIsUnexpected) {
  EXPECT_TRUE(is_unexpected(known_property_meaning(FAN_1_LEVEL_ID, std::to_string(MAX_LEVEL + 1)),
                            std::to_string(MAX_LEVEL + 1)));
  EXPECT_TRUE(is_unexpected(known_property_meaning(FAN_1_LEVEL_ID, "254"), "254"));
  EXPECT_TRUE(is_unexpected(known_property_meaning(FAN_1_LEVEL_ID, "256"), "256"));
}

TEST(KnownPropertyMeaning, ALevelThatIsNotAWholeDecimalIsUnexpectedWithTheTrimmedValue) {
  EXPECT_TRUE(is_unexpected(known_property_meaning(FAN_1_LEVEL_ID, ""), ""));
  EXPECT_TRUE(is_unexpected(known_property_meaning(FAN_1_LEVEL_ID, "   "), ""));
  EXPECT_TRUE(is_unexpected(known_property_meaning(FAN_1_LEVEL_ID, " 3a "), "3a"));
  EXPECT_TRUE(is_unexpected(known_property_meaning(FAN_1_LEVEL_ID, "-3"), "-3"));
  EXPECT_TRUE(is_unexpected(known_property_meaning(FAN_1_LEVEL_ID, "+3"), "+3"));
  EXPECT_TRUE(is_unexpected(known_property_meaning(FAN_1_LEVEL_ID, "3.0"), "3.0"));
  EXPECT_TRUE(is_unexpected(known_property_meaning(FAN_1_LEVEL_ID, "3 4"), "3 4"));
}

// A digit string longer than any int must not wrap around into a valid level.
TEST(KnownPropertyMeaning, AnOverlongDigitStringIsUnexpected) {
  EXPECT_TRUE(is_unexpected(known_property_meaning(FAN_1_LEVEL_ID, "4294967303"), "4294967303"));  // 2^32 + 7
  EXPECT_TRUE(is_unexpected(known_property_meaning(FAN_1_LEVEL_ID, "99999999999999999999"), "99999999999999999999"));
  EXPECT_TRUE(is_unexpected(known_property_meaning(FAN_1_LABEL_ID, "18446744073709551640"),  // 2^64 + 24
                            "18446744073709551640"));
}

// ── fan labels ──

TEST(KnownPropertyMeaning, NamesALabelByItsNameMappingEntry) {
  EXPECT_TRUE(means_name(known_property_meaning(FAN_1_LABEL_ID, "0"), NAME_MAPPING[0]));
  EXPECT_TRUE(means_name(known_property_meaning(FAN_1_LABEL_ID, "24"), NAME_MAPPING[24]));
  EXPECT_TRUE(means_name(known_property_meaning(FAN_6_LABEL_ID, std::to_string(NAME_MAPPING_COUNT - 1)),
                         NAME_MAPPING[NAME_MAPPING_COUNT - 1]));
}

TEST(KnownPropertyMeaning, ReadsALabelAsTheWholeTrimmedDecimal) {
  EXPECT_TRUE(means_name(known_property_meaning(FAN_1_LABEL_ID, "039     "), NAME_MAPPING[39]));
}

TEST(KnownPropertyMeaning, ALabelOutsideNameMappingOrNotADecimalIsUnexpected) {
  EXPECT_TRUE(is_unexpected(known_property_meaning(FAN_1_LABEL_ID, std::to_string(NAME_MAPPING_COUNT)),
                            std::to_string(NAME_MAPPING_COUNT)));
  EXPECT_TRUE(is_unexpected(known_property_meaning(FAN_1_LABEL_ID, "-1"), "-1"));
  EXPECT_TRUE(is_unexpected(known_property_meaning(FAN_1_LABEL_ID, "Bad"), "Bad"));
  EXPECT_TRUE(is_unexpected(known_property_meaning(FAN_1_LABEL_ID, ""), ""));
}

// ── summer ventilation ──

TEST(KnownPropertyMeaning, SummerVentilationMatchesTheRawOnAndOffStrings) {
  EXPECT_TRUE(means_name(known_property_meaning(SUMMER_VENTILATION_PROPERTY_ID, SUMMER_VENTILATION_ON), "on"));
  EXPECT_TRUE(means_name(known_property_meaning(SUMMER_VENTILATION_PROPERTY_ID, SUMMER_VENTILATION_OFF), "off"));
  EXPECT_TRUE(means_name(known_property_meaning(SUMMER_VENTILATION_PROPERTY_ID, " 0A00   "), "on"));
}

// The hub's atoi parse turns "0A00" / "0800" into 0 / 800; those are not the raw values.
TEST(KnownPropertyMeaning, SummerVentilationOtherValuesAreUnexpected) {
  EXPECT_TRUE(is_unexpected(known_property_meaning(SUMMER_VENTILATION_PROPERTY_ID, "0"), "0"));
  EXPECT_TRUE(is_unexpected(known_property_meaning(SUMMER_VENTILATION_PROPERTY_ID, "800"), "800"));
  EXPECT_TRUE(is_unexpected(known_property_meaning(SUMMER_VENTILATION_PROPERTY_ID, "0a00"), "0a00"));
  EXPECT_TRUE(is_unexpected(known_property_meaning(SUMMER_VENTILATION_PROPERTY_ID, ""), ""));
}

// ── date and clock ──

TEST(KnownPropertyMeaning, DateAndClockAreTheTrimmedValueAsSent) {
  EXPECT_TRUE(means(known_property_meaning(DATE_PROPERTY_ID, "16.11.26  "), MeaningKind::AS_SENT, "16.11.26"));
  EXPECT_TRUE(means(known_property_meaning(CLOCK_PROPERTY_ID, " 13:47:00"), MeaningKind::AS_SENT, "13:47:00"));
  EXPECT_TRUE(means(known_property_meaning(CLOCK_PROPERTY_ID, "garbage"), MeaningKind::AS_SENT, "garbage"));
  EXPECT_TRUE(means(known_property_meaning(DATE_PROPERTY_ID, ""), MeaningKind::AS_SENT, ""));
}

// AS_SENT and UNEXPECTED texts are views into the caller's raw value: nothing is copied.
TEST(KnownPropertyMeaning, TheTrimmedTextIsAViewIntoTheRawValue) {
  const std::string raw = "  13:47:00  ";
  auto meaning = known_property_meaning(CLOCK_PROPERTY_ID, raw);
  ASSERT_TRUE(meaning.has_value());
  EXPECT_EQ(meaning->text.data(), raw.data() + 2);
}

TEST(KnownPropertyMeaning, ANameIsTheStaticTableEntry) {
  auto meaning = known_property_meaning(FAN_1_LEVEL_ID, std::to_string(BURST_LEVEL));
  ASSERT_TRUE(meaning.has_value());
  EXPECT_EQ(meaning->text.data(), BURST_NAME);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
