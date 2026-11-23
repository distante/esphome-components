import { test } from "node:test";
import assert from "node:assert/strict";

import { knownIds } from "./known_meaning.mjs";
import { captureProgress, runIds, scanProgress } from "./scan_progress.mjs";
import { FIXTURE_CATALOGUE } from "./fixture_catalogue.mjs";

// The date and clock moved below the levels, so a check has known IDs before ID 10 as well as after it: the known
// IDs are 3, 5, 10, 11, 20, 21, 30.
const CATALOGUE = { ...FIXTURE_CATALOGUE, dateId: 3, clockId: 5 };
const KNOWN_COUNT = knownIds(CATALOGUE).length;
const { discover: DISCOVER, check: CHECK } = CATALOGUE.goals;

const discover = (fields) => ({
  active: false,
  goal: DISCOVER,
  from: 1,
  to: 100,
  ids: null,
  next: null,
  done: false,
  ...fields,
});
const check = (fields) => ({
  active: false,
  goal: CHECK,
  from: null,
  to: null,
  ids: null,
  next: null,
  done: false,
  ...fields,
});
const onDiscover = (scan) => scanProgress(scan, CATALOGUE, DISCOVER);
const onCheck = (scan) => scanProgress(scan, CATALOGUE, CHECK);

test("a running scan names the ID it queries next and the range", () => {
  assert.deepEqual(onDiscover(discover({ active: true, next: 26 })), {
    label: "Scanning ID 26 of 1–100",
    percent: 25,
  });
});

test("a stopped scan names the ID it stopped at", () => {
  assert.deepEqual(onDiscover(discover({ next: 51 })), { label: "Stopped at ID 51", percent: 50 });
});

test("a completed scan is complete and full", () => {
  assert.deepEqual(onDiscover(discover({ done: true })), { label: "Scan of 1–100 complete", percent: 100 });
});

test("before any run the scan is not scanned yet and empty", () => {
  assert.deepEqual(onDiscover(discover({ from: null, to: null })), { label: "Not scanned yet", percent: 0 });
});

test("before any run the check tab shows not scanned yet too, without a goal prefix", () => {
  assert.deepEqual(onCheck(discover({ from: null, to: null })), { label: "Not scanned yet", percent: 0 });
});

test("a scan querying its last ID is not yet full", () => {
  assert.equal(onDiscover(discover({ active: true, from: 1, to: 4, next: 4 })).percent, 75);
});

test("a running check counts the next ID among the known IDs, not a range", () => {
  assert.deepEqual(onCheck(check({ active: true, next: 10 })), {
    label: `Scanning ID 10 (3 of ${KNOWN_COUNT} known IDs)`,
    percent: Math.round((100 * 2) / KNOWN_COUNT),
  });
});

test("a check querying its first ID is empty", () => {
  assert.equal(onCheck(check({ active: true, next: 3 })).percent, 0);
});

test("a stopped check names the ID it stopped at", () => {
  assert.deepEqual(onCheck(check({ next: 20 })), {
    label: "Stopped at ID 20",
    percent: Math.round((100 * 4) / KNOWN_COUNT),
  });
});

test("a completed check counts the known IDs", () => {
  assert.deepEqual(onCheck(check({ done: true })), {
    label: `Check of ${KNOWN_COUNT} known IDs complete`,
    percent: 100,
  });
});

test("a scan of the other goal is labelled with its goal", () => {
  assert.deepEqual(onDiscover(check({ active: true, next: 10 })), {
    label: `Check scan: Scanning ID 10 (3 of ${KNOWN_COUNT} known IDs)`,
    percent: Math.round((100 * 2) / KNOWN_COUNT),
  });
  assert.equal(onCheck(discover({ next: 51 })).label, "Discover scan: Stopped at ID 51");
  assert.equal(onCheck(discover({ done: true })).label, "Discover scan: Scan of 1–100 complete");
});

test("the percent stays within 0–100 for a next ID outside the range or the known IDs", () => {
  for (const scan of [
    discover({ active: true, next: 0 }),
    discover({ active: true, next: 255 }),
    check({ active: true, next: 0 }),
    check({ active: true, next: 255 }),
    check({ active: true, next: 12 }),
  ]) {
    const { percent } = scanProgress(scan, CATALOGUE, scan.goal);
    assert.ok(percent >= 0 && percent <= 100, `${JSON.stringify(scan)}: ${percent}`);
  }
});

test("a check past every known ID counts no further than the known IDs", () => {
  assert.equal(
    onCheck(check({ active: true, next: 255 })).label,
    `Scanning ID 255 (${KNOWN_COUNT} of ${KNOWN_COUNT} known IDs)`,
  );
});

// ───── list runs (a narrowed capture) ─────

const listRun = (fields) => discover({ from: null, to: null, ids: [12, 3, 40], ...fields });

test("a run's IDs are its range or its list, minus the known IDs, ascending", () => {
  assert.deepEqual(runIds(CATALOGUE, discover({ from: 1, to: 12 })), [1, 2, 4, 6, 7, 8, 9, 12]);
  assert.deepEqual(runIds(CATALOGUE, listRun({})), [12, 40]);
  assert.deepEqual(runIds(CATALOGUE, check({})), knownIds(CATALOGUE));
  assert.equal(runIds(CATALOGUE, discover({ from: null, to: null })), null);
});

test("a running list run counts the next ID among its listed IDs", () => {
  assert.deepEqual(onDiscover(listRun({ active: true, next: 40 })), {
    label: "Scanning ID 40 (2 of 2 listed IDs)",
    percent: 50,
  });
});

test("a stopped list run names the ID it stopped at", () => {
  assert.deepEqual(onDiscover(listRun({ next: 40 })), { label: "Stopped at ID 40", percent: 50 });
});

test("a completed list run counts its listed IDs", () => {
  assert.deepEqual(onDiscover(listRun({ done: true })), { label: "Scan of 2 listed IDs complete", percent: 100 });
});

// ───── this page's capture ─────

const boost = (full) => ({ label: "boost", experimentName: "fan 1 mode", full });

test("a capture of the full range counts the IDs behind the scan", () => {
  assert.deepEqual(captureProgress(CATALOGUE, boost(true), discover({ active: true, from: 1, to: 12, next: 6 })), {
    label: "capturing “boost” for fan 1 mode · 3/8 · full range",
    percent: 38,
  });
});

test("a narrowed capture counts its candidates", () => {
  assert.deepEqual(captureProgress(CATALOGUE, boost(false), listRun({ active: true, next: 40 })), {
    label: "capturing “boost” for fan 1 mode · 1/2 · 2 candidates",
    percent: 50,
  });
});

test("a capture whose run is over counts every ID", () => {
  assert.deepEqual(captureProgress(CATALOGUE, boost(false), listRun({ done: true })), {
    label: "capturing “boost” for fan 1 mode · 2/2 · 2 candidates",
    percent: 100,
  });
});

test("the check goal's name comes from the catalogue", () => {
  const renamed = { ...CATALOGUE, goals: { discover: "find", check: "confirm" } };

  assert.deepEqual(scanProgress(check({ goal: "confirm", done: true }), renamed, "confirm"), {
    label: `Check of ${KNOWN_COUNT} known IDs complete`,
    percent: 100,
  });
});
