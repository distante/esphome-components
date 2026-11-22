import { test } from "node:test";
import assert from "node:assert/strict";

import { allPass, matchesFilter, visibleIds } from "./filters.mjs";
import { markNow } from "./mark.mjs";
import { emptyCopy } from "./records.mjs";
import { FIXTURE_CATALOGUE } from "./fixture_catalogue.mjs";

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

test("the status filter keeps only the chosen query statuses", () => {
  const filter = { ...allPass(), statuses: new Set(["nak", "timeout"]) };

  assert.equal(matchesFilter(row({ status: "timeout" }), filter, NOW_MS), true);
  assert.equal(matchesFilter(row({ status: "nak" }), filter, NOW_MS), true);
  assert.equal(matchesFilter(row({ status: "ok" }), filter, NOW_MS), false);
});

// Registered properties are known, and discover never shows known ones.
test("there is no hide-registered filter", () => {
  assert.deepEqual(Object.keys(allPass()).sort(), [
    "changedWithinMs",
    "onlyChangedSinceMark",
    "query",
    "statuses",
    "types",
  ]);
});

test("changed recently keeps IDs whose value changed within the window", () => {
  const filter = { ...allPass(), changedWithinMs: 5 * MINUTE_MS };

  assert.equal(matchesFilter(row({ changeCount: 2, changedAtMs: NOW_MS - 4 * MINUTE_MS }), filter, NOW_MS), true);
  assert.equal(matchesFilter(row({ changeCount: 2, changedAtMs: NOW_MS - 6 * MINUTE_MS }), filter, NOW_MS), false);
  // First seen recently, never changed.
  assert.equal(matchesFilter(row({ changeCount: 0, changedAtMs: NOW_MS - MINUTE_MS }), filter, NOW_MS), false);
});

/** A copy with just the raw values the mark tests need: markNow() reads only `records[id].raw`. */
function copyOf(raws) {
  const records = Object.fromEntries(Object.entries(raws).map(([id, raw]) => [id, { id: Number(id), raw }]));
  return { ...emptyCopy(), records };
}

test("discover shows no known ID, whatever the filter", () => {
  const [level] = FIXTURE_CATALOGUE.levelIds;
  const rows = [row({ id: 5 }), row({ id: level }), row({ id: FIXTURE_CATALOGUE.clockId }), row({ id: 200 })];

  assert.deepEqual([...visibleIds(FIXTURE_CATALOGUE, rows, allPass(), NOW_MS, null)], [5, 200]);
  const byQuery = { ...allPass(), query: String(level) };
  assert.deepEqual([...visibleIds(FIXTURE_CATALOGUE, rows, byQuery, NOW_MS, null)], []);
});

test("discover's visible IDs are the unknown rows that pass the filter", () => {
  const rows = [row({ id: 5, status: "ok" }), row({ id: 6, status: "nak" })];
  const filter = { ...allPass(), statuses: new Set(["nak"]) };

  assert.deepEqual([...visibleIds(FIXTURE_CATALOGUE, rows, filter, NOW_MS, null)], [6]);
});

test("only changed since mark keeps the IDs whose raw value differs from the mark", () => {
  const mark = markNow(copyOf({ 5: "0100", 6: "1", 7: null }), NOW_MS);
  const filter = { ...allPass(), onlyChangedSinceMark: true };

  assert.equal(matchesFilter(row({ id: 5, raw: "0200" }), filter, NOW_MS, mark), true);
  assert.equal(matchesFilter(row({ id: 6, raw: "1" }), filter, NOW_MS, mark), false);
  assert.equal(matchesFilter(row({ id: 7, raw: "9" }), filter, NOW_MS, mark), true);
  assert.equal(matchesFilter(row({ id: 8, raw: null, type: null }), filter, NOW_MS, mark), false);
  // Unticked, unchanged IDs pass.
  assert.equal(matchesFilter(row({ id: 6, raw: "1" }), allPass(), NOW_MS, mark), true);
});

test("only changed since mark adds to the other filters", () => {
  const mark = markNow(copyOf({ 5: "0100", 6: "1" }), NOW_MS);
  const filter = { ...allPass(), onlyChangedSinceMark: true, types: new Set(["int"]) };

  assert.equal(matchesFilter(row({ id: 5, raw: "0200", type: "padded int" }), filter, NOW_MS, mark), false);
  assert.equal(matchesFilter(row({ id: 6, raw: "2", type: "int" }), filter, NOW_MS, mark), true);
  assert.equal(matchesFilter(row({ id: 6, raw: "1", type: "int" }), filter, NOW_MS, mark), false);
});

test("only changed since mark lets every row through while no mark is set", () => {
  const filter = { ...allPass(), onlyChangedSinceMark: true };

  assert.equal(matchesFilter(row(), filter, NOW_MS, null), true);
});
