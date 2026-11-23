import { test } from "node:test";
import assert from "node:assert/strict";

import { allPass, feedIds, matchesFilter, visibleIds } from "./filters.mjs";
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
  assert.equal(matchesFilter(row(), allPass(), NOW_MS, null), true);
  assert.equal(matchesFilter(row({ raw: null, type: null, status: "nak" }), allPass(), NOW_MS, null), true);
});

test("search matches the exact ID, part of the trimmed value, or part of the note, ignoring case", () => {
  const filter = (query) => ({ ...allPass(), query });
  const padded = row({ id: 85, raw: "00               ", note: "Summer Vent" });

  assert.equal(matchesFilter(padded, filter("85"), NOW_MS, null), true);
  assert.equal(matchesFilter(padded, filter("8"), NOW_MS, null), false);
  assert.equal(matchesFilter(padded, filter("00"), NOW_MS, null), true);
  assert.equal(matchesFilter(padded, filter("summer"), NOW_MS, null), true);
  assert.equal(matchesFilter(padded, filter("winter"), NOW_MS, null), false);
});

test("the type filter keeps only the chosen types", () => {
  const filter = { ...allPass(), types: new Set(["date"]) };

  assert.equal(matchesFilter(row({ type: "date" }), filter, NOW_MS, null), true);
  assert.equal(matchesFilter(row({ type: "text" }), filter, NOW_MS, null), false);
});

test("the status filter keeps only the chosen query statuses", () => {
  const filter = { ...allPass(), statuses: new Set(["nak", "timeout"]) };

  assert.equal(matchesFilter(row({ status: "timeout" }), filter, NOW_MS, null), true);
  assert.equal(matchesFilter(row({ status: "nak" }), filter, NOW_MS, null), true);
  assert.equal(matchesFilter(row({ status: "ok" }), filter, NOW_MS, null), false);
});

// Registered properties are known, and discover never shows known ones.
test("no filter hides a registered row", () => {
  const registered = row({ registered: true });

  assert.equal(matchesFilter(registered, allPass(), NOW_MS, null), true);
  // hideRegistered is a removed filter key, kept to prove a stale stored filter hides nothing.
  assert.equal(matchesFilter(registered, { ...allPass(), hideRegistered: true }, NOW_MS, null), true);
});

test("changed recently keeps IDs whose value changed within the window", () => {
  const filter = { ...allPass(), changedWithinMs: 5 * MINUTE_MS };

  assert.equal(matchesFilter(row({ changeCount: 2, changedAtMs: NOW_MS - 4 * MINUTE_MS }), filter, NOW_MS, null), true);
  assert.equal(matchesFilter(row({ changeCount: 2, changedAtMs: NOW_MS - 6 * MINUTE_MS }), filter, NOW_MS, null), false);
  // First seen recently, never changed.
  assert.equal(matchesFilter(row({ changeCount: 0, changedAtMs: NOW_MS - MINUTE_MS }), filter, NOW_MS, null), false);
});

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

test("only candidates keeps the open experiment's candidates and corroborated candidates", () => {
  const filter = { ...allPass(), onlyCandidates: true };
  const candidates = new Set([5, 7]);

  assert.equal(matchesFilter(row({ id: 5 }), filter, NOW_MS, candidates), true);
  assert.equal(matchesFilter(row({ id: 6 }), filter, NOW_MS, candidates), false);
  // Unticked, every row passes.
  assert.equal(matchesFilter(row({ id: 6 }), allPass(), NOW_MS, candidates), true);
});

test("only candidates adds to the other filters", () => {
  const filter = { ...allPass(), onlyCandidates: true, statuses: new Set(["nak"]) };

  assert.equal(matchesFilter(row({ id: 5, status: "ok" }), filter, NOW_MS, new Set([5])), false);
  assert.equal(matchesFilter(row({ id: 5, status: "nak" }), filter, NOW_MS, new Set([5])), true);
});

// With no experiment open the checkbox is disabled: a stale tick filters nothing.
test("only candidates lets every row through while no experiment is open", () => {
  const filter = { ...allPass(), onlyCandidates: true };

  assert.equal(matchesFilter(row(), filter, NOW_MS, null), true);
});

test("the feed ignores only candidates but keeps every other filter", () => {
  const rows = [row({ id: 5, status: "ok" }), row({ id: 6, status: "nak" })];
  const candidatesOnly = { ...allPass(), onlyCandidates: true };

  assert.deepEqual([...visibleIds(FIXTURE_CATALOGUE, rows, candidatesOnly, NOW_MS, new Set([6]))], [6]);
  assert.deepEqual([...feedIds(FIXTURE_CATALOGUE, rows, candidatesOnly, NOW_MS, new Set([6]))], [5, 6]);
  const candidatesOnlyOk = { ...candidatesOnly, statuses: new Set(["ok"]) };
  assert.deepEqual([...feedIds(FIXTURE_CATALOGUE, rows, candidatesOnlyOk, NOW_MS, new Set([6]))], [5]);
});
