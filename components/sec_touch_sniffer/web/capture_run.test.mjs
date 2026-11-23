import { test } from "node:test";
import assert from "node:assert/strict";

import { captureStep, isCaptureRun } from "./capture_run.mjs";
import { applyResponse, emptyCopy } from "./records.mjs";
import { deviceRecord, deviceResponse, FIXTURE_BOOT } from "./fixture_records.mjs";
import { FIXTURE_CATALOGUE } from "./fixture_catalogue.mjs";
import { captureInFlight } from "./fixture_experiments.mjs";

const CATALOGUE = FIXTURE_CATALOGUE;
const NOW_MS = Date.UTC(2026, 10, 22, 9, 30, 0);
// A full capture of 9–12 queries 9 and 12 only: the fixture catalogue's level IDs are 10 and 11.
const [KNOWN_ID] = CATALOGUE.levelIds;

const runningScan = (fields) => ({
  active: true,
  goal: CATALOGUE.goals.discover,
  from: 9,
  to: 12,
  ids: null,
  next: 9,
  done: false,
  ...fields,
});
const doneScan = (fields) => runningScan({ active: false, next: null, done: true, ...fields });

const FULL = { full: true, from: 9, to: 12 };
const NARROWED = { full: false, ids: [12] };
const pendingCapture = (next = FULL) => captureInFlight(next);

function copyWith(scan, records = [], fields = {}) {
  return applyResponse(emptyCopy(), deviceResponse(records, { boot: FIXTURE_BOOT, uptime: 20_000, scan, ...fields }), NOW_MS)
    .copy;
}

const ANSWERED = [
  deviceRecord(9, "01"),
  deviceRecord(KNOWN_ID, "05"),
  deviceRecord(12, "0A00", { status: "nak" }),
  deviceRecord(40, "07"),
];

test("a poll from before the capture was accepted means nothing yet", () => {
  assert.deepEqual(captureStep(CATALOGUE, pendingCapture(), copyWith(doneScan({})), false, NOW_MS), { kind: "wait" });
});

test("while the capture's run is active the page waits", () => {
  assert.deepEqual(captureStep(CATALOGUE, pendingCapture(), copyWith(runningScan({})), true, NOW_MS), {
    kind: "wait",
  });
});

test("a completed run stores a capture with an entry for each queried ID", () => {
  const step = captureStep(CATALOGUE, pendingCapture(), copyWith(doneScan({}), ANSWERED), true, NOW_MS);

  assert.deepEqual(step, {
    kind: "store",
    range: { from: 9, to: 12 },
    capture: {
      label: " boost ",
      atMs: NOW_MS,
      uptimeMs: 20_000,
      boot: FIXTURE_BOOT,
      full: true,
      entries: { 9: { outcome: "ok", raw: "01" }, 12: { outcome: "nak", raw: null } },
    },
  });
});

test("a completed narrowed capture stores only its listed IDs and fixes no range", () => {
  const copy = copyWith(doneScan({ from: null, to: null, ids: [12] }), ANSWERED);

  const step = captureStep(CATALOGUE, pendingCapture(NARROWED), copy, true, NOW_MS);

  assert.equal(step.kind, "store");
  assert.equal(step.range, null);
  assert.equal(step.capture.full, false);
  assert.deepEqual(step.capture.entries, { 12: { outcome: "nak", raw: null } });
});

test("a timed-out ID is stored as a timeout without a value", () => {
  const copy = copyWith(doneScan({}), [deviceRecord(9, "01", { status: "timeout" })]);

  assert.deepEqual(captureStep(CATALOGUE, pendingCapture(), copy, true, NOW_MS).capture.entries, {
    9: { outcome: "timeout", raw: null },
  });
});

test("a raw value keeps its control and non-ASCII bytes", () => {
  const raw = "0\u0001ÿ ";
  const copy = copyWith(doneScan({}), [deviceRecord(9, raw)]);

  assert.deepEqual(captureStep(CATALOGUE, pendingCapture(), copy, true, NOW_MS).capture.entries[9], {
    outcome: "ok",
    raw,
  });
});

test("a run that stopped before its end stores nothing", () => {
  const copy = copyWith(runningScan({ active: false, next: 12 }), ANSWERED);

  assert.deepEqual(captureStep(CATALOGUE, pendingCapture(), copy, true, NOW_MS), { kind: "abandon", why: "stopped" });
});

test("a boot change (reboot or Clear data) abandons the capture, even while the run goes on", () => {
  const copy = copyWith(runningScan({}), [], { boot: "0badf00d" });

  assert.deepEqual(captureStep(CATALOGUE, pendingCapture(), copy, true, NOW_MS), {
    kind: "abandon",
    why: "boot changed",
  });
});

test("a completed run of another range or list is not this capture", () => {
  for (const scan of [doneScan({ from: 1 }), doneScan({ from: null, to: null, ids: [9, 12] })]) {
    assert.deepEqual(captureStep(CATALOGUE, pendingCapture(), copyWith(scan), true, NOW_MS), {
      kind: "abandon",
      why: "another run",
    });
  }
  const otherList = copyWith(doneScan({ from: null, to: null, ids: [9] }));
  assert.equal(captureStep(CATALOGUE, pendingCapture(NARROWED), otherList, true, NOW_MS).kind, "abandon");
});

test("before the device answered at all the page waits", () => {
  assert.deepEqual(captureStep(CATALOGUE, pendingCapture(), emptyCopy(), true, NOW_MS), { kind: "wait" });
});

// ───── whether the scan row shows the capture ─────

test("a fresh poll of the capture's own run shows the capture while active and once done", () => {
  assert.equal(isCaptureRun(CATALOGUE, pendingCapture(), runningScan({}), true), true);
  assert.equal(isCaptureRun(CATALOGUE, pendingCapture(), doneScan({}), true), true);
});

test("a narrowed capture's run shows it whatever order the device lists its IDs in", () => {
  const narrowed = pendingCapture({ full: false, ids: [9, 12] });

  assert.equal(isCaptureRun(CATALOGUE, narrowed, runningScan({ from: null, to: null, ids: [12, 9] }), true), true);
});

test("the run before the capture (another range or list) does not show it", () => {
  for (const scan of [runningScan({ from: 1 }), doneScan({ from: null, to: null, ids: [9, 12] })]) {
    assert.equal(isCaptureRun(CATALOGUE, pendingCapture(), scan, true), false);
  }
  const otherList = doneScan({ from: null, to: null, ids: [9] });
  assert.equal(isCaptureRun(CATALOGUE, pendingCapture(NARROWED), otherList, true), false);
});

test("a check does not show the capture", () => {
  assert.equal(isCaptureRun(CATALOGUE, pendingCapture(), runningScan({ goal: CATALOGUE.goals.check }), true), false);
});

test("a stopped run of the capture's range does not show it", () => {
  assert.equal(isCaptureRun(CATALOGUE, pendingCapture(), runningScan({ active: false, next: 12 }), true), false);
});

test("a poll too early to show the capture does not show its run as done: it is the same list's run before", () => {
  const sameListAgain = doneScan({ from: null, to: null, ids: [12] });

  assert.equal(isCaptureRun(CATALOGUE, pendingCapture(NARROWED), sameListAgain, false), false);
});

test("an active run of the capture shows it from any poll: the device runs one at a time", () => {
  assert.equal(isCaptureRun(CATALOGUE, pendingCapture(), runningScan({}), false), true);
});
