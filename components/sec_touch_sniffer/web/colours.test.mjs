import { test } from "node:test";
import assert from "node:assert/strict";

import { STATUS_COLOURS, TYPE_COLOURS } from "./colours.mjs";
import { STATUSES } from "./records.mjs";
import { TYPES } from "./type_guess.mjs";

test("every guessed type has a colour, and only those", () => {
  assert.deepEqual(Object.keys(TYPE_COLOURS).sort(), [...TYPES].sort());
});

test("every query status has a colour, and only those", () => {
  assert.deepEqual(Object.keys(STATUS_COLOURS).sort(), [...STATUSES].sort());
});

// Colouring by type or by status must not make a type and a status look the same.
test("no colour is both a type's and a status's", () => {
  const typeColours = new Set(Object.values(TYPE_COLOURS).map((colour) => colour.toLowerCase()));
  const shared = Object.values(STATUS_COLOURS).filter((colour) => typeColours.has(colour.toLowerCase()));
  assert.deepEqual(shared, []);
});
