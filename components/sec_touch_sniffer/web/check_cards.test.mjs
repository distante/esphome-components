import { test } from "node:test";
import assert from "node:assert/strict";

import { checkCards } from "./check_cards.mjs";
import { FIXTURE_CATALOGUE } from "./fixture_catalogue.mjs";

const CATALOGUE = FIXTURE_CATALOGUE;
const [LEVEL_1, LEVEL_2] = CATALOGUE.levelIds;
const [LABEL_1, LABEL_2] = CATALOGUE.labelIds;
const [ENABLED_1, ENABLED_2] = CATALOGUE.timeProgramEnabledIds;
const [TIME_1_1, TIME_1_2, TIME_2_1, TIME_2_2] = CATALOGUE.timeProgramTimeIds;
const [LEVEL_1_1, LEVEL_1_2, LEVEL_2_1, LEVEL_2_2] = CATALOGUE.timeProgramLevelIds;
const NOT_SEEN_AT = (id) => ({ id, raw: null, meaning: null });

/** A row as rowsOf() gives it, only the fields the cards read. */
const row = (id, raw, registered = false) => ({ id, raw, registered });

const named = (text) => ({ text, unexpected: false });

test("one card per fan, in fan order, with its level and label", () => {
  const cards = checkCards(CATALOGUE, [row(LEVEL_1, "7"), row(LABEL_1, "02  "), row(LEVEL_2, "0"), row(LABEL_2, "1")]);

  assert.deepEqual(cards.fans.map(({ timeProgram, ...card }) => card), [
    {
      fan: 1,
      registered: false,
      level: { id: LEVEL_1, raw: "7", meaning: named("Burst") },
      label: { id: LABEL_1, raw: "02  ", meaning: named("Küche") },
    },
    {
      fan: 2,
      registered: false,
      level: { id: LEVEL_2, raw: "0", meaning: named("off") },
      label: { id: LABEL_2, raw: "1", meaning: named("Bad") },
    },
  ]);
});

test("each fan card has its pair's time program: the enabled mask and switch times in order", () => {
  const cards = checkCards(CATALOGUE, [
    row(ENABLED_1, "03"),
    row(TIME_1_1, "08:30:00"),
    row(TIME_1_2, "22:00:00  "),
    row(LEVEL_1_1, "3"),
    row(LEVEL_1_2, "0"),
    row(ENABLED_2, "00"),
    row(TIME_2_1, "06:00:00"),
    row(LEVEL_2_2, "7"),
  ]);
  const names = CATALOGUE.enabledMaskNames;

  assert.deepEqual(cards.fans.map((card) => card.timeProgram), [
    {
      enabled: { id: ENABLED_1, raw: "03", meaning: named(`1${names.separator}2${names.suffix}`) },
      switchTimes: [
        {
          switchTime: 1,
          time: { id: TIME_1_1, raw: "08:30:00", meaning: named("08:30:00") },
          level: { id: LEVEL_1_1, raw: "3", meaning: named(CATALOGUE.levelNames[3]) },
        },
        {
          switchTime: 2,
          time: { id: TIME_1_2, raw: "22:00:00  ", meaning: named("22:00:00") },
          level: { id: LEVEL_1_2, raw: "0", meaning: named(CATALOGUE.levelNames[0]) },
        },
      ],
    },
    {
      enabled: { id: ENABLED_2, raw: "00", meaning: named(names.none) },
      switchTimes: [
        {
          switchTime: 1,
          time: { id: TIME_2_1, raw: "06:00:00", meaning: named("06:00:00") },
          level: NOT_SEEN_AT(LEVEL_2_1),
        },
        {
          switchTime: 2,
          time: NOT_SEEN_AT(TIME_2_2),
          level: { id: LEVEL_2_2, raw: "7", meaning: named(CATALOGUE.levelNames[7]) },
        },
      ],
    },
  ]);
});

// The spec's examples: a level above the highest switch-time level, a mask with a bit above the switch times (20),
// hour 24.
test("a time-program value its rule does not match is unexpected", () => {
  const overLevel = String(CATALOGUE.maxSwitchTimeLevel + 1);
  const cards = checkCards(CATALOGUE, [row(ENABLED_1, "20"), row(TIME_1_1, "24:00:00"), row(LEVEL_1_1, overLevel)]);
  const [first] = cards.fans[0].timeProgram.switchTimes;

  assert.deepEqual(cards.fans[0].timeProgram.enabled.meaning, { text: "unexpected value 20", unexpected: true });
  assert.deepEqual(first.time.meaning, { text: "unexpected value 24:00:00", unexpected: true });
  assert.deepEqual(first.level.meaning, { text: `unexpected value ${overLevel}`, unexpected: true });
});

test("a fan is registered when the hub registered its level or its label", () => {
  const cards = checkCards(CATALOGUE, [
    row(LEVEL_1, "7", true),
    row(LABEL_1, "1"),
    row(LEVEL_2, "0"),
    row(LABEL_2, "1", true),
  ]);

  assert.deepEqual(cards.fans.map((card) => card.registered), [true, true]);
  assert.equal(checkCards(CATALOGUE, [row(LEVEL_1, "7")]).fans[0].registered, false);
});

test("the global card holds summer ventilation, date and clock, each with meaning and raw", () => {
  const cards = checkCards(CATALOGUE, [
    row(CATALOGUE.clockId, "13:47:00"),
    row(CATALOGUE.summerVentilationId, "0A00"),
    row(CATALOGUE.dateId, "16.11.26 "),
  ]);

  assert.deepEqual(cards.global, [
    { name: "summer ventilation", id: CATALOGUE.summerVentilationId, raw: "0A00", meaning: named("on") },
    { name: "date", id: CATALOGUE.dateId, raw: "16.11.26 ", meaning: named("16.11.26") },
    { name: "clock", id: CATALOGUE.clockId, raw: "13:47:00", meaning: named("13:47:00") },
  ]);
});

test("a value matching no entry is unexpected", () => {
  const cards = checkCards(CATALOGUE, [row(LEVEL_1, "99"), row(CATALOGUE.summerVentilationId, "0810")]);

  assert.deepEqual(cards.fans[0].level.meaning, { text: "unexpected value 99", unexpected: true });
  assert.deepEqual(cards.global[0].meaning, { text: "unexpected value 0810", unexpected: true });
});

test("a known property with no row or no value yet is not seen: no raw, no meaning", () => {
  const cards = checkCards(CATALOGUE, [row(LABEL_1, null)]);

  assert.deepEqual(cards.fans[0].level, { id: LEVEL_1, raw: null, meaning: null });
  assert.deepEqual(cards.fans[0].label, { id: LABEL_1, raw: null, meaning: null });
  assert.deepEqual(cards.global[2], { name: "clock", id: CATALOGUE.clockId, raw: null, meaning: null });
});

test("unknown rows are ignored", () => {
  assert.deepEqual(checkCards(CATALOGUE, [row(5, "1"), row(200, "x")]), checkCards(CATALOGUE, []));
});
