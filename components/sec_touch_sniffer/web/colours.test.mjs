import { test } from "node:test";
import assert from "node:assert/strict";

import { STATUS_COLOURS, TYPE_COLOURS } from "./colours.mjs";
import { STATUSES } from "./filters.mjs";
import { TYPES } from "./type_guess.mjs";

test("every guessed type has a colour, and only those", () => {
  assert.deepEqual(Object.keys(TYPE_COLOURS).sort(), [...TYPES].sort());
});

test("every query status has a colour, and only those", () => {
  assert.deepEqual(Object.keys(STATUS_COLOURS).sort(), [...STATUSES].sort());
});
