import { test } from "node:test";
import assert from "node:assert/strict";

import { drawerBadges, STALE_BADGE_TEXT } from "./drawer_head.mjs";
import { TYPE_COLOURS } from "./colours.mjs";

/** A row as rowsOf() gives it, only the fields the badges read. */
const row = (type, stale = false) => ({ type, stale });

test("a fresh row with a type shows only its type, in the type's colour", () => {
  assert.deepEqual(drawerBadges(row("int")), [{ text: "int", colour: TYPE_COLOURS.int }]);
});

test("a stale row with a type shows its type, then the uncoloured stale badge", () => {
  assert.deepEqual(drawerBadges(row("date", true)), [
    { text: "date", colour: TYPE_COLOURS.date },
    { text: STALE_BADGE_TEXT, colour: null },
  ]);
});

test("a row with no type shows no type badge, and nothing at all when it is also fresh", () => {
  assert.deepEqual(drawerBadges(row(null, true)), [{ text: STALE_BADGE_TEXT, colour: null }]);
  assert.deepEqual(drawerBadges(row(null)), []);
});
