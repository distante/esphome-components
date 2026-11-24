import { test } from "node:test";
import assert from "node:assert/strict";

import { applyResponse, emptyCopy } from "./records.mjs";
import { deviceRecord, deviceResponse } from "./fixture_records.mjs";
import {
  CANDIDATE_ID,
  experimentWith,
  NAK,
  pendingProbe,
  PROBE_ORIGINAL,
  probeEntry,
  TIMEOUT,
  walkOf,
} from "./fixture_experiments.mjs";
import {
  defaultOptions,
  deviceKey,
  LAST_MAC_KEY,
  OPTIONS_KEY,
  optionsText,
  parseOptions,
  parseStored,
  STORAGE_PREFIX,
  storedText,
} from "./storage.mjs";
import { allPass } from "./filters.mjs";
import { STATUSES } from "./records.mjs";
import { TYPES } from "./type_guess.mjs";

const BROWSER_NOW_MS = Date.UTC(2026, 10, 21, 12, 0, 0);

function deviceCopy() {
  const record = deviceRecord(5, "0100", { last: 5000, changed: 3000, hist: [[3000, "0100"]] });
  return applyResponse(emptyCopy(), deviceResponse([record]), BROWSER_NOW_MS).copy;
}

// The catalogue's default discover range, made up so the tests don't lean on the real one.
const DEFAULT_RANGE = { from: 7, to: 99 };

const nothing = () => ({
  copy: emptyCopy(),
  notes: {},
  mutes: new Set(),
  range: DEFAULT_RANGE,
  experiments: [],
  pendingProbe: null,
});

/** What a device stored with `fields` (and the fixture copy) reloads as. */
const roundTrip = (fields) =>
  parseStored(storedText({ ...nothing(), copy: deviceCopy(), ...fields }), DEFAULT_RANGE);

test("every key shares the storage prefix", () => {
  assert.ok(LAST_MAC_KEY.startsWith(STORAGE_PREFIX));
  assert.equal(deviceKey("aabbccddeeff"), `${STORAGE_PREFIX}aabbccddeeff`);
  assert.notEqual(deviceKey("aabbccddeeff"), LAST_MAC_KEY);
});

test("a stored device reloads with its copy, notes, mutes and discover range", () => {
  const range = { from: 3, to: 200 };
  const stored = roundTrip({ notes: { 5: "Fan level?" }, mutes: new Set([5, 9]), range });

  assert.equal(stored.copy.records[5].raw, "0100");
  assert.equal(stored.copy.since, null);
  assert.deepEqual(stored.notes, { 5: "Fan level?" });
  assert.deepEqual(stored.mutes, new Set([5, 9]));
  assert.deepEqual(stored.range, range);
});

test("a device stored without a discover range gets the default range", () => {
  const text = JSON.stringify({ notes: { 5: "x" } });

  assert.deepEqual(parseStored(text, DEFAULT_RANGE).range, DEFAULT_RANGE);
});

test("a stored range that is not two integers gives the default range", () => {
  const broken = [null, "1-255", [1, 255], {}, { from: 1 }, { to: 255 }, { from: "1", to: 255 }, { from: 1.5, to: 255 }];
  for (const range of broken) {
    assert.deepEqual(parseStored(JSON.stringify({ range }), DEFAULT_RANGE).range, DEFAULT_RANGE, JSON.stringify(range));
  }
});

// The device decides which ranges are valid; storage only checks the shape.
test("a stored range of two integers is kept whatever its bounds", () => {
  const range = { from: 300, to: 2 };

  assert.deepEqual(parseStored(JSON.stringify({ range }), DEFAULT_RANGE).range, range);
});

test("nothing stored, or text that is not JSON, gives nothing", () => {
  assert.deepEqual(parseStored(null, DEFAULT_RANGE), nothing());
  assert.deepEqual(parseStored("{not json", DEFAULT_RANGE), nothing());
  assert.deepEqual(parseStored("5", DEFAULT_RANGE), nothing());
  assert.deepEqual(parseStored("[1, 2]", DEFAULT_RANGE), nothing());
});

test("notes keep only text values keyed by an ID", () => {
  const text = JSON.stringify({ notes: { 5: "Fan level?", 6: 7, 7: null, 8: { a: 1 }, x: "not an ID" } });

  assert.deepEqual(parseStored(text, DEFAULT_RANGE).notes, { 5: "Fan level?" });
  assert.deepEqual(parseStored(JSON.stringify({ notes: "nope" }), DEFAULT_RANGE).notes, {});
  assert.deepEqual(parseStored(JSON.stringify({ notes: ["a"] }), DEFAULT_RANGE).notes, {});
});

test("mutes that are not an array of integer IDs are dropped", () => {
  for (const mutes of [5, "5", null, { 5: true }, [5, "6"], [1.5]]) {
    assert.deepEqual(parseStored(JSON.stringify({ mutes }), DEFAULT_RANGE).mutes, new Set(), JSON.stringify(mutes));
  }
});

// ───── experiments and the pending probe ─────

const PROBE_ENTRY = probeEntry({});
// A restore to the original, as the page stores it before the device reports it.
const PENDING = pendingProbe({ kind: "restore", value: PROBE_ORIGINAL });

function storedExperiment() {
  const experiment = walkOf(CANDIDATE_ID, [["normal", "01"], ["boost", NAK], ["eco", TIMEOUT], ["off", "0\u0001ÿ"]]);
  return { ...experiment, probes: [PROBE_ENTRY, { ...PROBE_ENTRY, verdict: null, restore: null, interrupted: true }] };
}

test("experiments and the pending probe reload as stored", () => {
  const experiments = [storedExperiment(), experimentWith([], { range: null })];

  const stored = roundTrip({ experiments, pendingProbe: PENDING });

  assert.deepEqual(stored.experiments, experiments);
  assert.deepEqual(stored.pendingProbe, PENDING);
});

test("a device stored without experiments or a pending probe has none", () => {
  assert.deepEqual(nothing().experiments, []);
  assert.equal(nothing().pendingProbe, null);
  assert.deepEqual(parseStored(JSON.stringify({ notes: {} }), DEFAULT_RANGE).experiments, []);
  assert.deepEqual(parseStored(JSON.stringify({ experiments: "x" }), DEFAULT_RANGE).experiments, []);
});

test("an unreadable experiment is dropped on its own", () => {
  const good = storedExperiment();
  const broken = [null, "x", { ...good, key: 5 }, { ...good, name: null }, { ...good, captures: "none" },
    { ...good, range: { from: 1 } }, { ...good, createdAtMs: "today" }, { ...good, probes: {} }];
  const text = JSON.stringify({ experiments: [...broken, good] });

  assert.deepEqual(parseStored(text, DEFAULT_RANGE).experiments, [good]);
});

test("an unreadable capture or probe entry is dropped from its experiment on its own", () => {
  const good = storedExperiment();
  const [capture] = good.captures;
  const badCaptures = [{ ...capture, label: 3 }, { ...capture, full: "yes" }, { ...capture, entries: [] },
    { ...capture, entries: { x: { outcome: "ok", raw: "1" } } }, { ...capture, entries: { 5: { outcome: "maybe", raw: null } } },
    { ...capture, atMs: null }];
  const badProbes = [{ ...PROBE_ENTRY, steps: "none" }, { ...PROBE_ENTRY, verdict: "great" }, { ...PROBE_ENTRY, atMs: null }];
  const text = JSON.stringify({
    experiments: [{ ...good, captures: [...badCaptures, ...good.captures], probes: [...badProbes, ...good.probes] }],
  });

  assert.deepEqual(parseStored(text, DEFAULT_RANGE).experiments, [good]);
});

test("an unreadable pending probe is dropped", () => {
  for (const pendingProbe of [5, { ...PENDING, kind: "poke" }, { ...PENDING, id: String(CANDIDATE_ID) }, { ...PENDING, steps: null },
    { ...PENDING, experimentKey: null }]) {
    assert.equal(parseStored(JSON.stringify({ pendingProbe }), DEFAULT_RANGE).pendingProbe, null,
      JSON.stringify(pendingProbe));
  }
});

test("a corrupt copy gives an empty copy but keeps the notes and mutes", () => {
  const stored = parseStored(JSON.stringify({ copy: "nope", notes: { 5: "x" }, mutes: [5] }), DEFAULT_RANGE);

  assert.deepEqual(stored.copy, emptyCopy());
  assert.deepEqual(stored.notes, { 5: "x" });
  assert.deepEqual(stored.mutes, new Set([5]));
});

test("every checkbox starts off and every filter chip ticked", () => {
  const filter = allPass();
  assert.deepEqual(defaultOptions(), {
    showKnown: false,
    includeMuted: false,
    onlyOpen: false,
    onlyCandidates: filter.onlyCandidates,
    types: filter.types,
    statuses: filter.statuses,
  });
});

test("the checkboxes survive a round trip through storage", () => {
  const options = {
    showKnown: true,
    includeMuted: true,
    onlyOpen: true,
    onlyCandidates: true,
    types: new Set([TYPES[0]]),
    statuses: new Set(),
  };

  assert.deepEqual(parseOptions(optionsText(options)), options);
});

test("nothing stored or unreadable storage gives the defaults", () => {
  for (const text of [null, "", "{not json", "[]", "42"]) {
    assert.deepEqual(parseOptions(text), defaultOptions(), String(text));
  }
});

test("each unreadable checkbox falls back to its default on its own", () => {
  const text = JSON.stringify({
    showKnown: "yes",
    includeMuted: true,
    untickedTypes: "int",
    untickedStatuses: [STATUSES[0], "gone", 7],
  });

  assert.deepEqual(parseOptions(text), {
    ...defaultOptions(),
    includeMuted: true,
    statuses: new Set(STATUSES.slice(1)),
  });
});

// A type or status added in a later firmware is in no stored list, so it starts ticked.
test("a chip missing from the stored unticked chips comes back ticked", () => {
  const text = JSON.stringify({ untickedTypes: [TYPES[0]], untickedStatuses: [] });

  const options = parseOptions(text);

  assert.deepEqual(options.types, new Set(TYPES.slice(1)));
  assert.deepEqual(options.statuses, new Set(STATUSES));
});

test("the options entry is not a device entry", () => {
  assert.ok(OPTIONS_KEY.startsWith(STORAGE_PREFIX));
  assert.notEqual(OPTIONS_KEY, LAST_MAC_KEY);
});
