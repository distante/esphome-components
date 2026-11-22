import { test } from "node:test";
import assert from "node:assert/strict";

import { checkCards } from "./check_cards.mjs";

// A small catalogue in the shape catalogue.mjs has, with made-up IDs so the tests don't lean on the real ones.
const CATALOGUE = {
  levelIds: [10, 11],
  labelIds: [20, 21],
  summerVentilationId: 30,
  dateId: 31,
  clockId: 32,
  levelNames: { 0: "off", 7: "Burst" },
  labelNames: ["-", "Bad", "Küche"],
  summerVentilationNames: { "0A00": "on", "0800": "off" },
  discoverRange: { from: 1, to: 255 },
};
const [LEVEL_1, LEVEL_2] = CATALOGUE.levelIds;
const [LABEL_1, LABEL_2] = CATALOGUE.labelIds;

/** A row as rowsOf() gives it, only the fields the cards read. */
const row = (id, raw, registered = false) => ({ id, raw, registered });

const named = (text) => ({ text, unexpected: false });

test("one card per fan, in fan order, with its level and label", () => {
  const cards = checkCards(CATALOGUE, [row(LEVEL_1, "7"), row(LABEL_1, "02  "), row(LEVEL_2, "0"), row(LABEL_2, "1")]);

  assert.deepEqual(cards.fans, [
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
