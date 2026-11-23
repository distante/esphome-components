import { test } from "node:test";
import assert from "node:assert/strict";

import { captureStep } from "./capture_run.mjs";
import { applyResponse, emptyCopy } from "./records.mjs";
import { deviceRecord, deviceResponse } from "./fixture_records.mjs";
import { FIXTURE_CATALOGUE } from "./fixture_catalogue.mjs";

const CATALOGUE = FIXTURE_CATALOGUE;
const BOOT = "9f2c41ab";
const NOW_MS = Date.UTC(2026, 10, 22, 9, 30, 0);
// The fixture catalogue's level IDs are 10 and 11: a full capture of 9–12 queries 9 and 12 only.
const KNOWN_ID = 10;

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
const pendingCapture = (next = FULL) => ({ experimentKey: "e1", experimentName: "fan 1 mode", label: " Boost ", next, boot: BOOT });

function copyWith(scan, records = [], fields = {}) {
  return applyResponse(emptyCopy(), deviceResponse(records, { boot: BOOT, uptime: 20_000, scan, ...fields }), NOW_MS)
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
      label: " Boost ",
      atMs: NOW_MS,
      uptimeMs: 20_000,
      boot: BOOT,
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
