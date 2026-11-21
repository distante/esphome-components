import { test } from "node:test";
import assert from "node:assert/strict";

import { allPass, matchesFilter } from "./filters.mjs";

const NOW_MS = Date.UTC(2026, 10, 21, 12, 0, 0);
const MINUTE_MS = 60_000;

function row(overrides = {}) {
  return {
    id: 55,
    raw: "0.360.00:00",
    type: "text",
    registered: false,
    status: "ok",
    changeCount: 0,
    changedAtMs: NOW_MS - 60 * MINUTE_MS,
    note: "",
    ...overrides,
  };
}

test("the default filter lets every row through", () => {
  assert.equal(matchesFilter(row(), allPass(), NOW_MS), true);
  assert.equal(matchesFilter(row({ raw: null, type: null, status: "nak" }), allPass(), NOW_MS), true);
});

test("search matches the exact ID, part of the trimmed value, or part of the note, ignoring case", () => {
  const filter = (query) => ({ ...allPass(), query });
  const padded = row({ id: 85, raw: "00               ", note: "Summer Vent" });

  assert.equal(matchesFilter(padded, filter("85"), NOW_MS), true);
  assert.equal(matchesFilter(padded, filter("8"), NOW_MS), false);
  assert.equal(matchesFilter(padded, filter("00"), NOW_MS), true);
  assert.equal(matchesFilter(padded, filter("summer"), NOW_MS), true);
  assert.equal(matchesFilter(padded, filter("winter"), NOW_MS), false);
});

test("the type filter keeps only the chosen types", () => {
  const filter = { ...allPass(), types: new Set(["date"]) };

  assert.equal(matchesFilter(row({ type: "date" }), filter, NOW_MS), true);
  assert.equal(matchesFilter(row({ type: "text" }), filter, NOW_MS), false);
});

test("the status filter keeps only that query status", () => {
  const filter = { ...allPass(), status: "timeout" };

  assert.equal(matchesFilter(row({ status: "timeout" }), filter, NOW_MS), true);
  assert.equal(matchesFilter(row({ status: "ok" }), filter, NOW_MS), false);
});

test("hide registered drops registered IDs", () => {
  const filter = { ...allPass(), hideRegistered: true };

  assert.equal(matchesFilter(row({ registered: true }), filter, NOW_MS), false);
  assert.equal(matchesFilter(row({ registered: false }), filter, NOW_MS), true);
});

test("changed recently keeps IDs whose value changed within the window", () => {
  const filter = { ...allPass(), changedWithinMs: 5 * MINUTE_MS };

  assert.equal(matchesFilter(row({ changeCount: 2, changedAtMs: NOW_MS - 4 * MINUTE_MS }), filter, NOW_MS), true);
  assert.equal(matchesFilter(row({ changeCount: 2, changedAtMs: NOW_MS - 6 * MINUTE_MS }), filter, NOW_MS), false);
  // First seen recently, never changed.
  assert.equal(matchesFilter(row({ changeCount: 0, changedAtMs: NOW_MS - MINUTE_MS }), filter, NOW_MS), false);
});
