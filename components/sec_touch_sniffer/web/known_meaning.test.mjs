import { test } from "node:test";
import assert from "node:assert/strict";

import { isKnownId, knownIds, knownMeaning } from "./known_meaning.mjs";

// A small catalogue in the shape catalogue.mjs has, with made-up IDs so the tests don't lean on the real ones.
const CATALOGUE = {
  levelIds: [10, 11],
  labelIds: [20, 21],
  summerVentilationId: 30,
  dateId: 31,
  clockId: 32,
  levelNames: { 0: "off", 3: "level 3", 7: "Burst", 255: "not connected" },
  labelNames: ["-", "Bad", "Küche"],
  summerVentilationNames: { "0A00": "on", "0800": "off" },
};
const [LEVEL, OTHER_LEVEL] = CATALOGUE.levelIds;
const [LABEL] = CATALOGUE.labelIds;

const named = (text) => ({ text, unexpected: false });
const unexpected = (trimmed) => ({ text: `unexpected value ${trimmed}`, unexpected: true });

test("every ID of the catalogue is known, and only those", () => {
  for (const id of [...CATALOGUE.levelIds, ...CATALOGUE.labelIds]) {
    assert.equal(isKnownId(CATALOGUE, id), true, `ID ${id}`);
  }
  assert.equal(isKnownId(CATALOGUE, CATALOGUE.summerVentilationId), true);
  assert.equal(isKnownId(CATALOGUE, CATALOGUE.dateId), true);
  assert.equal(isKnownId(CATALOGUE, CATALOGUE.clockId), true);
  for (const id of [0, 1, 12, 19, 22, 33, 255]) {
    assert.equal(isKnownId(CATALOGUE, id), false, `ID ${id}`);
  }
});

test("the known IDs are every ID of the catalogue, once each, in ascending order", () => {
  assert.deepEqual(knownIds(CATALOGUE), [10, 11, 20, 21, 30, 31, 32]);
  const unsorted = { ...CATALOGUE, levelIds: [40, 12], dateId: 5, clockId: 1 };
  assert.deepEqual(knownIds(unsorted), [1, 5, 12, 20, 21, 30, 40]);
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
