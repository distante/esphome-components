import { test } from "node:test";
import assert from "node:assert/strict";

import {
  isKnownId,
  knownCellTitle,
  knownIds,
  knownMeaning,
  meaningText,
  NOT_SEEN,
  SWITCH_TIME_ENABLED,
  SWITCH_TIME_NOT_ENABLED,
  switchTimeEnabled,
} from "./known_meaning.mjs";
import { escapeControlBytes } from "./control_bytes.mjs";
import { FIXTURE_CATALOGUE } from "./fixture_catalogue.mjs";

const CATALOGUE = FIXTURE_CATALOGUE;
const [LEVEL, OTHER_LEVEL] = CATALOGUE.levelIds;
const [LABEL] = CATALOGUE.labelIds;
const [ENABLED, OTHER_ENABLED] = CATALOGUE.timeProgramEnabledIds;
const [TIME] = CATALOGUE.timeProgramTimeIds;
const OTHER_TIME = CATALOGUE.timeProgramTimeIds.at(-1);
const [SWITCH_LEVEL] = CATALOGUE.timeProgramLevelIds;
const OTHER_SWITCH_LEVEL = CATALOGUE.timeProgramLevelIds.at(-1);
const MASK_NAMES = CATALOGUE.enabledMaskNames;

const named = (text) => ({ text, unexpected: false });
const unexpected = (trimmed) => ({ text: `unexpected value ${trimmed}`, unexpected: true });
/** An enabled mask's name: the switch times' numbers joined by the separator, then the suffix. */
const enabledName = (...switchTimes) => named(switchTimes.join(MASK_NAMES.separator) + MASK_NAMES.suffix);

test("every ID of the catalogue is known, and only those", () => {
  for (const id of [...CATALOGUE.levelIds, ...CATALOGUE.labelIds]) {
    assert.equal(isKnownId(CATALOGUE, id), true, `ID ${id}`);
  }
  assert.equal(isKnownId(CATALOGUE, CATALOGUE.summerVentilationId), true);
  assert.equal(isKnownId(CATALOGUE, CATALOGUE.dateId), true);
  assert.equal(isKnownId(CATALOGUE, CATALOGUE.clockId), true);
  const timeProgramIds = [...CATALOGUE.timeProgramEnabledIds, ...CATALOGUE.timeProgramTimeIds,
    ...CATALOGUE.timeProgramLevelIds];
  for (const id of timeProgramIds) {
    assert.equal(isKnownId(CATALOGUE, id), true, `ID ${id}`);
  }
  for (const id of [0, 1, 12, 19, 22, 33, 59, 70, 255]) {
    assert.equal(isKnownId(CATALOGUE, id), false, `ID ${id}`);
  }
});

test("the known IDs are every ID of the catalogue, once each, in ascending order", () => {
  assert.deepEqual(knownIds(CATALOGUE), [10, 11, 20, 21, 30, 31, 32, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69]);
  const unsorted = { ...CATALOGUE, levelIds: [40, 12], dateId: 5, clockId: 1, timeProgramEnabledIds: [3],
    timeProgramTimeIds: [70, 2], timeProgramLevelIds: [4] };
  assert.deepEqual(knownIds(unsorted), [1, 2, 3, 4, 5, 12, 20, 21, 30, 40, 70]);
});

test("an unknown ID or a never-seen value has no meaning", () => {
  assert.equal(knownMeaning(CATALOGUE, 12, "3"), null);
  assert.equal(knownMeaning(CATALOGUE, LEVEL, null), null);
  assert.equal(knownMeaning(CATALOGUE, CATALOGUE.dateId, null), null);
});

test("a level is named by the trimmed raw read as a whole decimal", () => {
  assert.deepEqual(knownMeaning(CATALOGUE, LEVEL, "0"), named("off"));
  assert.deepEqual(knownMeaning(CATALOGUE, OTHER_LEVEL, "7"), named("Burst"));
  assert.deepEqual(knownMeaning(CATALOGUE, LEVEL, "255"), named("not connected"));
  assert.deepEqual(knownMeaning(CATALOGUE, LEVEL, "03"), named("level 3"));
  assert.deepEqual(knownMeaning(CATALOGUE, LEVEL, "00               "), named("off"));
  assert.deepEqual(knownMeaning(CATALOGUE, LEVEL, " \t7\r\n"), named("Burst"));
});

test("a level that is not a whole decimal in the table is unexpected, with the trimmed raw", () => {
  for (const raw of ["", "   ", "1", "12", "256", " 3a ", "-3", "+3", "3.0", "3 4", "1e1", "0x7", "99999999999999999999"]) {
    assert.deepEqual(knownMeaning(CATALOGUE, LEVEL, raw), unexpected(raw.trim()), JSON.stringify(raw));
  }
});

// Object.prototype names must not count as level names.
test("a level that names an object property is unexpected", () => {
  assert.deepEqual(knownMeaning(CATALOGUE, LEVEL, "constructor"), unexpected("constructor"));
});

test("a label is the label name at the trimmed raw read as a whole decimal", () => {
  assert.deepEqual(knownMeaning(CATALOGUE, LABEL, "0"), named("-"));
  assert.deepEqual(knownMeaning(CATALOGUE, LABEL, "02   "), named("Küche"));
});

test("a label outside the names or not a whole decimal is unexpected", () => {
  for (const raw of ["3", "-1", "Bad", "", "1.0", "length"]) {
    assert.deepEqual(knownMeaning(CATALOGUE, LABEL, raw), unexpected(raw), JSON.stringify(raw));
  }
});

test("summer ventilation matches the raw on and off strings", () => {
  const id = CATALOGUE.summerVentilationId;
  assert.deepEqual(knownMeaning(CATALOGUE, id, "0A00"), named("on"));
  assert.deepEqual(knownMeaning(CATALOGUE, id, " 0800  "), named("off"));
  for (const raw of ["0", "800", "0a00", ""]) {
    assert.deepEqual(knownMeaning(CATALOGUE, id, raw), unexpected(raw), JSON.stringify(raw));
  }
});

test("date and clock mean their trimmed value as sent", () => {
  assert.deepEqual(knownMeaning(CATALOGUE, CATALOGUE.dateId, "16.11.26  "), named("16.11.26"));
  assert.deepEqual(knownMeaning(CATALOGUE, CATALOGUE.clockId, " 13:47:00"), named("13:47:00"));
  assert.deepEqual(knownMeaning(CATALOGUE, CATALOGUE.clockId, "garbage"), named("garbage"));
});

// The device's C++ lookup trims ASCII whitespace only; a non-breaking space is part of the value.
test("only ASCII whitespace is trimmed, as on the device", () => {
  assert.deepEqual(knownMeaning(CATALOGUE, LEVEL, " 7"), unexpected(" 7"));
  assert.deepEqual(knownMeaning(CATALOGUE, CATALOGUE.dateId, "\v16.11.26\f"), named("16.11.26"));
});

test("a switch time's level up to the highest switch-time level has its level name", () => {
  assert.deepEqual(knownMeaning(CATALOGUE, SWITCH_LEVEL, "0"), named(CATALOGUE.levelNames[0]));
  assert.deepEqual(knownMeaning(CATALOGUE, OTHER_SWITCH_LEVEL, "03  "), named(CATALOGUE.levelNames[3]));
  assert.deepEqual(knownMeaning(CATALOGUE, SWITCH_LEVEL, String(CATALOGUE.maxSwitchTimeLevel)),
    named(CATALOGUE.levelNames[CATALOGUE.maxSwitchTimeLevel]));
});

// The fixture's 255 has a level name, as on the device, yet no switch time takes it.
test("a switch time's level above the highest switch-time level or not a whole decimal is unexpected", () => {
  for (const raw of [String(CATALOGUE.maxSwitchTimeLevel + 1), "255", "1", "", "3a", "-3", "constructor"]) {
    assert.deepEqual(knownMeaning(CATALOGUE, SWITCH_LEVEL, raw), unexpected(raw), JSON.stringify(raw));
  }
});

test("an enabled mask names the switch times of its bits", () => {
  assert.deepEqual(knownMeaning(CATALOGUE, ENABLED, "01"), enabledName(1));
  assert.deepEqual(knownMeaning(CATALOGUE, ENABLED, "02"), enabledName(2));
  assert.deepEqual(knownMeaning(CATALOGUE, OTHER_ENABLED, "03"), enabledName(1, 2));
});

test("an enabled mask of zero is none enabled", () => {
  assert.deepEqual(knownMeaning(CATALOGUE, ENABLED, "00"), named(MASK_NAMES.none));
});

test("an enabled mask is two trimmed hex digits in either case", () => {
  const fiveSwitchTimes = { ...CATALOGUE, switchTimeCount: 5 };
  assert.deepEqual(knownMeaning(fiveSwitchTimes, ENABLED, "1F"), enabledName(1, 2, 3, 4, 5));
  assert.deepEqual(knownMeaning(fiveSwitchTimes, ENABLED, " 1f "), enabledName(1, 2, 3, 4, 5));
  assert.deepEqual(knownMeaning(fiveSwitchTimes, ENABLED, "0c"), enabledName(3, 4));
  assert.deepEqual(knownMeaning(fiveSwitchTimes, ENABLED, "18"), enabledName(4, 5));
});

// The spec's unexpected example is 20 with 5 switch times: the bit after switch time 5's.
test("an enabled mask with a bit above the switch times is unexpected", () => {
  const fiveSwitchTimes = { ...CATALOGUE, switchTimeCount: 5 };
  for (const raw of ["20", "3F", "FF", "ff"]) {
    assert.deepEqual(knownMeaning(fiveSwitchTimes, ENABLED, raw), unexpected(raw), raw);
  }
  assert.deepEqual(knownMeaning(CATALOGUE, ENABLED, "04"), unexpected("04"));
});

test("an enabled mask that is not two hex digits is unexpected", () => {
  for (const raw of ["", "3", "003", "0x3", "G0", "0g", "-1", "+3", "0 3"]) {
    assert.deepEqual(knownMeaning(CATALOGUE, ENABLED, raw), unexpected(raw), JSON.stringify(raw));
  }
});

test("a switch time's time is the trimmed HH:MM:SS as sent", () => {
  assert.deepEqual(knownMeaning(CATALOGUE, TIME, "08:30:00"), named("08:30:00"));
  assert.deepEqual(knownMeaning(CATALOGUE, OTHER_TIME, " 00:00:00   "), named("00:00:00"));
  assert.deepEqual(knownMeaning(CATALOGUE, TIME, "23:59:59"), named("23:59:59"));
});

// The spec's unexpected example is 24:00:00.
test("a switch time's time out of range or not exactly HH:MM:SS is unexpected", () => {
  for (const raw of ["24:00:00", "12:60:00", "12:00:60", "99:99:99", "", "8:30:00", "08:30", "08:30:00:00",
    "08:30:0", "08-30-00", "0830:00", "ab:cd:ef", "08:3a:00", "+8:30:00", "08:30 00", "16.11.26", "٠٨:٣٠:٠٠"]) {
    assert.deepEqual(knownMeaning(CATALOGUE, TIME, raw), unexpected(raw), JSON.stringify(raw));
  }
});

// #139 item 3: the bounds are the catalogue's time units, not the page's own.
test("a switch time's time is checked against the catalogue's time units", () => {
  const shortDay = { ...CATALOGUE, timeUnits: { hoursPerDay: 12, minutesPerHour: 30, secondsPerMinute: 20 } };
  assert.deepEqual(knownMeaning(shortDay, TIME, "11:29:19"), named("11:29:19"));
  for (const raw of ["12:00:00", "00:30:00", "00:00:20"]) {
    assert.deepEqual(knownMeaning(shortDay, TIME, raw), unexpected(raw), raw);
  }
});

test("a switch time is enabled when its bit, N-1, of the pair's enabled mask is set", () => {
  assert.deepEqual(switchTimeEnabled(CATALOGUE, "01", 1), named(SWITCH_TIME_ENABLED));
  assert.deepEqual(switchTimeEnabled(CATALOGUE, "01", 2), named(SWITCH_TIME_NOT_ENABLED));
  assert.deepEqual(switchTimeEnabled(CATALOGUE, " 02 ", 1), named(SWITCH_TIME_NOT_ENABLED));
  assert.deepEqual(switchTimeEnabled(CATALOGUE, "02", 2), named(SWITCH_TIME_ENABLED));
  assert.deepEqual(switchTimeEnabled(CATALOGUE, "00", 1), named(SWITCH_TIME_NOT_ENABLED));
  const fiveSwitchTimes = { ...CATALOGUE, switchTimeCount: 5 };
  assert.deepEqual(switchTimeEnabled(fiveSwitchTimes, "1f", 5), named(SWITCH_TIME_ENABLED));
  assert.deepEqual(switchTimeEnabled(fiveSwitchTimes, "0F", 5), named(SWITCH_TIME_NOT_ENABLED));
});

// The mask rule decides: a mask it rejects gives no bit, so every switch time shows the mask as unexpected.
test("a switch time's enabled bit of an unexpected mask is unexpected", () => {
  for (const raw of ["04", "20", "3", "zz", ""]) {
    assert.deepEqual(switchTimeEnabled(CATALOGUE, raw, 1), unexpected(raw.trim()), JSON.stringify(raw));
  }
});

test("a switch time's enabled bit of a mask not read yet is unexpected and not seen", () => {
  assert.deepEqual(switchTimeEnabled(CATALOGUE, null, 1), { text: NOT_SEEN, unexpected: true });
});

test("a known cell's tooltip is its ID, its shown value and its meaning", () => {
  assert.equal(knownCellTitle(CATALOGUE, LEVEL, " 7  "), `${LEVEL} = 7 · Burst`);
  assert.equal(knownCellTitle(CATALOGUE, LABEL, "9"), `${LABEL} = 9 · unexpected value 9`);
  assert.equal(knownCellTitle(CATALOGUE, CATALOGUE.clockId, "13:47\x01"), `${CATALOGUE.clockId} = 13:47\\x01 · 13:47\\x01`);
});

test("a known cell with no value yet is not seen in its tooltip", () => {
  assert.equal(knownCellTitle(CATALOGUE, LEVEL, null), `${LEVEL} = — · ${NOT_SEEN}`);
});

test("no meaning is shown as not seen, a meaning as its text with control bytes escaped", () => {
  assert.equal(meaningText(null), NOT_SEEN);
  assert.equal(meaningText(named("Burst")), "Burst");
  assert.equal(meaningText(unexpected("9\x01")), escapeControlBytes("unexpected value 9\x01"));
});
