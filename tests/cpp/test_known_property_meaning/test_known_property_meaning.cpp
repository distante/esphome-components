#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>

#include "sec_touch/fan_level.h"
#include "sec_touch/known_property_meaning.h"

using esphome::sec_touch::AUTOMATIC_CO2_LEVEL;
using esphome::sec_touch::AUTOMATIC_CO2_NAME;
using esphome::sec_touch::AUTOMATIC_TIME_LEVEL;
using esphome::sec_touch::BURST_LEVEL;
using esphome::sec_touch::BURST_NAME;
using esphome::sec_touch::CLOCK_PROPERTY_ID;
using esphome::sec_touch::DATE_PROPERTY_ID;
using esphome::sec_touch::ENABLED_MASK_NONE_NAME;
using esphome::sec_touch::ENABLED_MASK_SEPARATOR;
using esphome::sec_touch::ENABLED_MASK_SUFFIX;
using esphome::sec_touch::FAN_LABEL_IDS;
using esphome::sec_touch::FAN_LEVEL_IDS;
using esphome::sec_touch::is_known_property;
using esphome::sec_touch::known_property_meaning;
using esphome::sec_touch::KnownMeaning;
using esphome::sec_touch::label_name;
using esphome::sec_touch::MAX_LEVEL;
using esphome::sec_touch::max_known_property_id;
using esphome::sec_touch::MAX_NORMAL_LEVEL;
using esphome::sec_touch::MAX_SWITCH_TIME_LEVEL;
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
using esphome::sec_touch::SUMMER_VENTILATION_NAMES;
using esphome::sec_touch::SUMMER_VENTILATION_OFF;
using esphome::sec_touch::SUMMER_VENTILATION_ON;
using esphome::sec_touch::SUMMER_VENTILATION_PROPERTY_ID;
using esphome::sec_touch::SWITCH_TIME_COUNT;
using esphome::sec_touch::TIME_PROGRAM_ENABLED_IDS;
using esphome::sec_touch::TIME_PROGRAM_LEVEL_IDS;
using esphome::sec_touch::TIME_PROGRAM_TIME_IDS;

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

/// The name SUMMER_VENTILATION_NAMES gives the entry whose raw value is `raw`, or nullptr when none has it.
const char *summer_ventilation_table_name(const char *raw) {
  for (const auto &entry : SUMMER_VENTILATION_NAMES) {
    if (std::string_view(entry.raw) == raw) {
      return entry.name;
    }
  }
  return nullptr;
}

const int FAN_1_LEVEL_ID = FAN_LEVEL_IDS.front();
const int FAN_6_LEVEL_ID = FAN_LEVEL_IDS.back();
const int FAN_1_LABEL_ID = FAN_LABEL_IDS.front();
const int FAN_6_LABEL_ID = FAN_LABEL_IDS.back();
const int PAIR_1_ENABLED_ID = TIME_PROGRAM_ENABLED_IDS.front();
const int PAIR_6_ENABLED_ID = TIME_PROGRAM_ENABLED_IDS.back();
const int PAIR_1_SWITCH_TIME_1_TIME_ID = TIME_PROGRAM_TIME_IDS.front();
const int PAIR_6_SWITCH_TIME_5_TIME_ID = TIME_PROGRAM_TIME_IDS.back();
const int PAIR_1_SWITCH_TIME_1_LEVEL_ID = TIME_PROGRAM_LEVEL_IDS.front();
const int PAIR_6_SWITCH_TIME_5_LEVEL_ID = TIME_PROGRAM_LEVEL_IDS.back();

/// An enabled mask's name: the switch times' numbers joined by ENABLED_MASK_SEPARATOR, then ENABLED_MASK_SUFFIX.
std::string enabled_name(std::initializer_list<int> switch_times) {
  std::string name;
  for (int n : switch_times) {
    name += (name.empty() ? "" : ENABLED_MASK_SEPARATOR) + std::to_string(n);
  }
  return name + ENABLED_MASK_SUFFIX;
}

}  // namespace

// ── label name ──

TEST(LabelName, NamesEveryNameMappingValue) {
  for (int value = 0; value < NAME_MAPPING_COUNT; value++) {
    EXPECT_EQ(label_name(value), NAME_MAPPING[value]) << value;
  }
}

TEST(LabelName, IsNullOutsideNameMapping) {
  EXPECT_EQ(label_name(-1), nullptr);
  EXPECT_EQ(label_name(NAME_MAPPING_COUNT), nullptr);
}

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

TEST(IsKnownProperty, KnowsEveryTimeProgramId) {
  for (int id : TIME_PROGRAM_ENABLED_IDS) {
    EXPECT_TRUE(is_known_property(id)) << id;
  }
  for (int id : TIME_PROGRAM_TIME_IDS) {
    EXPECT_TRUE(is_known_property(id)) << id;
  }
  for (int id : TIME_PROGRAM_LEVEL_IDS) {
    EXPECT_TRUE(is_known_property(id)) << id;
  }
}

TEST(IsKnownProperty, DoesNotKnowTheIdsAroundThem) {
  EXPECT_FALSE(is_known_property(0));
  EXPECT_FALSE(is_known_property(FAN_1_LEVEL_ID - 1));
  EXPECT_FALSE(is_known_property(FAN_6_LEVEL_ID + 1));
  EXPECT_FALSE(is_known_property(FAN_1_LABEL_ID - 1));
  EXPECT_FALSE(is_known_property(FAN_6_LABEL_ID + 1));
  EXPECT_FALSE(is_known_property(SUMMER_VENTILATION_PROPERTY_ID + 1));
  EXPECT_FALSE(is_known_property(TIME_PROGRAM_ENABLED_IDS.front() - 1));
  EXPECT_FALSE(is_known_property(TIME_PROGRAM_LEVEL_IDS.back() + 1));
  EXPECT_FALSE(is_known_property(255));
}

TEST(MaxKnownPropertyId, IsKnownAndNoKnownIdIsAboveIt) {
  const int max = max_known_property_id();
  EXPECT_TRUE(is_known_property(max)) << max;
  for (int id : FAN_LEVEL_IDS) {
    EXPECT_LE(id, max);
  }
  for (int id : FAN_LABEL_IDS) {
    EXPECT_LE(id, max);
  }
  for (int id : TIME_PROGRAM_ENABLED_IDS) {
    EXPECT_LE(id, max);
  }
  for (int id : TIME_PROGRAM_TIME_IDS) {
    EXPECT_LE(id, max);
  }
  for (int id : TIME_PROGRAM_LEVEL_IDS) {
    EXPECT_LE(id, max);
  }
  EXPECT_LE(SUMMER_VENTILATION_PROPERTY_ID, max);
  EXPECT_LE(DATE_PROPERTY_ID, max);
  EXPECT_LE(CLOCK_PROPERTY_ID, max);
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
  const char *on_name = summer_ventilation_table_name(SUMMER_VENTILATION_ON);
  const char *off_name = summer_ventilation_table_name(SUMMER_VENTILATION_OFF);
  ASSERT_NE(on_name, nullptr);
  ASSERT_NE(off_name, nullptr);
  EXPECT_TRUE(means_name(known_property_meaning(SUMMER_VENTILATION_PROPERTY_ID, SUMMER_VENTILATION_ON), on_name));
  EXPECT_TRUE(means_name(known_property_meaning(SUMMER_VENTILATION_PROPERTY_ID, SUMMER_VENTILATION_OFF), off_name));
  EXPECT_TRUE(means_name(known_property_meaning(SUMMER_VENTILATION_PROPERTY_ID, " 0A00   "), on_name));
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

// ── time-program levels ──

// A switch time takes the fan levels 0 to Automatic CO2, named as the fan level property names them.
TEST(KnownPropertyMeaning, ASwitchTimeLevelUpToAutomaticCo2HasTheFanLevelName) {
  for (int level = OFF_LEVEL; level <= MAX_SWITCH_TIME_LEVEL; level++) {
    const std::string raw = std::to_string(level);
    const auto fan_level_meaning = known_property_meaning(FAN_1_LEVEL_ID, raw);
    ASSERT_TRUE(fan_level_meaning.has_value()) << level;
    for (int id : TIME_PROGRAM_LEVEL_IDS) {
      EXPECT_TRUE(means_name(known_property_meaning(id, raw), fan_level_meaning->text)) << id << " " << level;
    }
  }
  EXPECT_TRUE(means_name(known_property_meaning(PAIR_1_SWITCH_TIME_1_LEVEL_ID, std::to_string(OFF_LEVEL)), OFF_NAME));
  EXPECT_TRUE(means_name(known_property_meaning(PAIR_6_SWITCH_TIME_5_LEVEL_ID, std::to_string(AUTOMATIC_CO2_LEVEL)),
                         AUTOMATIC_CO2_NAME));
}

TEST(KnownPropertyMeaning, ReadsASwitchTimeLevelAsTheWholeTrimmedDecimal) {
  EXPECT_TRUE(means_name(known_property_meaning(PAIR_1_SWITCH_TIME_1_LEVEL_ID, "07               "), BURST_NAME));
}

// The spec's unexpected examples: Automatik Zeit (10), Sleep (11) and not connected (255) are fan levels, but no
// switch time takes them.
TEST(KnownPropertyMeaning, ASwitchTimeLevelAboveAutomaticCo2OrNotADecimalIsUnexpected) {
  for (int level : {AUTOMATIC_TIME_LEVEL, SLEEP_LEVEL, NOT_CONNECTED_LEVEL}) {
    EXPECT_TRUE(is_unexpected(known_property_meaning(PAIR_1_SWITCH_TIME_1_LEVEL_ID, std::to_string(level)),
                              std::to_string(level)));
  }
  EXPECT_TRUE(is_unexpected(known_property_meaning(PAIR_1_SWITCH_TIME_1_LEVEL_ID, ""), ""));
  EXPECT_TRUE(is_unexpected(known_property_meaning(PAIR_1_SWITCH_TIME_1_LEVEL_ID, " 3a "), "3a"));
  EXPECT_TRUE(is_unexpected(known_property_meaning(PAIR_1_SWITCH_TIME_1_LEVEL_ID, "-1"), "-1"));
  EXPECT_TRUE(is_unexpected(known_property_meaning(PAIR_1_SWITCH_TIME_1_LEVEL_ID, "99999999999"), "99999999999"));
}

// ── time-program enabled masks ──

TEST(KnownPropertyMeaning, AnEnabledMaskNamesTheSwitchTimesOfItsBits) {
  EXPECT_TRUE(means_name(known_property_meaning(PAIR_1_ENABLED_ID, "01"), enabled_name({1})));
  EXPECT_TRUE(means_name(known_property_meaning(PAIR_1_ENABLED_ID, "02"), enabled_name({2})));
  EXPECT_TRUE(means_name(known_property_meaning(PAIR_1_ENABLED_ID, "03"), enabled_name({1, 2})));
  EXPECT_TRUE(means_name(known_property_meaning(PAIR_1_ENABLED_ID, "0C"), enabled_name({3, 4})));
  EXPECT_TRUE(means_name(known_property_meaning(PAIR_6_ENABLED_ID, "18"), enabled_name({4, 5})));
  EXPECT_TRUE(means_name(known_property_meaning(PAIR_6_ENABLED_ID, "1F"), enabled_name({1, 2, 3, 4, 5})));
}

TEST(KnownPropertyMeaning, AnEnabledMaskOfZeroIsNoneEnabled) {
  EXPECT_TRUE(means_name(known_property_meaning(PAIR_1_ENABLED_ID, "00"), ENABLED_MASK_NONE_NAME));
}

TEST(KnownPropertyMeaning, AnEnabledMaskIsTwoTrimmedHexDigitsInEitherCase) {
  EXPECT_TRUE(means_name(known_property_meaning(PAIR_1_ENABLED_ID, "1f"), enabled_name({1, 2, 3, 4, 5})));
  EXPECT_TRUE(means_name(known_property_meaning(PAIR_1_ENABLED_ID, "0c"), enabled_name({3, 4})));
  EXPECT_TRUE(means_name(known_property_meaning(PAIR_1_ENABLED_ID, " 03   "), enabled_name({1, 2})));
}

// The spec's unexpected example is 20: the bit after switch time 5's.
TEST(KnownPropertyMeaning, AnEnabledMaskWithABitAboveTheSwitchTimesIsUnexpected) {
  ASSERT_EQ(SWITCH_TIME_COUNT, 5);  // 1F is the highest mask with a bit for each switch time and no other
  for (const char *raw : {"20", "21", "3F", "80", "FF", "ff"}) {
    EXPECT_TRUE(is_unexpected(known_property_meaning(PAIR_1_ENABLED_ID, raw), raw)) << raw;
  }
}

TEST(KnownPropertyMeaning, AnEnabledMaskThatIsNotTwoHexDigitsIsUnexpected) {
  for (const char *raw : {"", "3", "003", "0x3", "G0", "0g", "-1", "+3", "0 3"}) {
    EXPECT_TRUE(is_unexpected(known_property_meaning(PAIR_1_ENABLED_ID, raw), raw)) << raw;
  }
}

TEST(KnownPropertyMeaning, AnEnabledMaskNameIsStatic) {
  const std::string raw = "03";
  auto meaning = known_property_meaning(PAIR_1_ENABLED_ID, raw);
  ASSERT_TRUE(meaning.has_value());
  EXPECT_NE(meaning->text.data(), raw.data());
  EXPECT_EQ(known_property_meaning(PAIR_6_ENABLED_ID, "03")->text.data(), meaning->text.data());
}

// ── time-program times ──

TEST(KnownPropertyMeaning, ASwitchTimeTimeIsTheTrimmedHhMmSsAsSent) {
  EXPECT_TRUE(
      means(known_property_meaning(PAIR_1_SWITCH_TIME_1_TIME_ID, "08:30:00"), MeaningKind::AS_SENT, "08:30:00"));
  EXPECT_TRUE(
      means(known_property_meaning(PAIR_6_SWITCH_TIME_5_TIME_ID, " 00:00:00   "), MeaningKind::AS_SENT, "00:00:00"));
  EXPECT_TRUE(
      means(known_property_meaning(PAIR_6_SWITCH_TIME_5_TIME_ID, "23:59:59"), MeaningKind::AS_SENT, "23:59:59"));
}

// The spec's unexpected example is 24:00:00.
TEST(KnownPropertyMeaning, ASwitchTimeTimeOutOfRangeIsUnexpected) {
  for (const char *raw : {"24:00:00", "12:60:00", "12:00:60", "99:99:99"}) {
    EXPECT_TRUE(is_unexpected(known_property_meaning(PAIR_1_SWITCH_TIME_1_TIME_ID, raw), raw)) << raw;
  }
}

TEST(KnownPropertyMeaning, ASwitchTimeTimeNotExactlyHhMmSsIsUnexpected) {
  for (const char *raw : {"", "8:30:00", "08:30", "08:30:00:00", "08:30:0", "08-30-00", "0830:00", "ab:cd:ef",
                          "08:3a:00", "+8:30:00", "08:30 00", "16.11.26"}) {
    EXPECT_TRUE(is_unexpected(known_property_meaning(PAIR_1_SWITCH_TIME_1_TIME_ID, raw), raw)) << raw;
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
