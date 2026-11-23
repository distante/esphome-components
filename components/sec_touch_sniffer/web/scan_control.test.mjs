import { test } from "node:test";
import assert from "node:assert/strict";

import {
  acceptedRange,
  BUSY_UNKNOWN,
  busyText,
  captureStartPath,
  isRunning,
  PROBE_REFUSED,
  probeError,
  RANGE_REFUSED,
  rangeError,
  runningText,
  scanButton,
  scanReply,
  scanStartPath,
  switchStopsScan,
} from "./scan_control.mjs";
import { FIXTURE_CATALOGUE } from "./fixture_catalogue.mjs";
import { CANDIDATE_ID } from "./fixture_experiments.mjs";

const CATALOGUE = FIXTURE_CATALOGUE;
const { discover: DISCOVER, check: CHECK } = CATALOGUE.goals;

const scan = (fields) => ({
  active: false,
  goal: DISCOVER,
  from: 1,
  to: 255,
  ids: null,
  next: null,
  done: false,
  ...fields,
});
const probe = (fields) => ({
  active: true,
  kind: "probe",
  id: CANDIDATE_ID,
  value: "03",
  original: null,
  steps: [],
  verdict: null,
  restore: null,
  ...fields,
});

test("a discover start sends the goal and the range as typed, URL-encoded", () => {
  assert.equal(scanStartPath(CATALOGUE, DISCOVER, "3", "200"), "/sniffer/scan/start?goal=discover&from=3&to=200");
  assert.equal(
    scanStartPath(CATALOGUE, DISCOVER, " 1&x", "2 0"),
    "/sniffer/scan/start?goal=discover&from=%201%26x&to=2%200",
  );
  assert.equal(scanStartPath(CATALOGUE, DISCOVER, "", "999"), "/sniffer/scan/start?goal=discover&from=&to=999");
});

test("a check start sends only the goal", () => {
  assert.equal(scanStartPath(CATALOGUE, CHECK, "3", "200"), "/sniffer/scan/start?goal=check");
});

test("the goal names come from the catalogue", () => {
  const renamed = { goals: { discover: "find", check: "confirm" } };

  assert.equal(scanStartPath(renamed, "find", "3", "200"), "/sniffer/scan/start?goal=find&from=3&to=200");
  assert.equal(scanStartPath(renamed, DISCOVER, "3", "200"), "/sniffer/scan/start?goal=discover");
  assert.deepEqual(acceptedRange(renamed, scan({ active: true, goal: "find", from: 3, to: 9 }), "3", "9"), {
    from: 3,
    to: 9,
  });
  assert.equal(acceptedRange(renamed, scan({ active: true, from: 3, to: 9 }), "3", "9"), null);
});

test("a capture start sends the full range, or only the candidates as an ID list", () => {
  assert.equal(
    captureStartPath(CATALOGUE, { full: true, from: 17, to: 200 }),
    "/sniffer/scan/start?goal=discover&from=17&to=200",
  );
  assert.equal(
    captureStartPath(CATALOGUE, { full: false, ids: [5, 12, 40] }),
    "/sniffer/scan/start?goal=discover&ids=5,12,40",
  );
});

const label = (scanState, goal, probeState = null) => scanButton(scanState, probeState, goal).label;

test("the start button names the open tab's goal", () => {
  assert.equal(label(scan({}), DISCOVER), "▶ Start discover scan");
  assert.equal(label(scan({ goal: DISCOVER, done: true }), CHECK), "▶ Start check scan");
});

test("the stop button always names the running scan's goal", () => {
  assert.equal(label(scan({ active: true }), DISCOVER), "■ Stop discover scan");
  assert.equal(label(scan({ active: true, goal: CHECK }), CHECK), "■ Stop check scan");
  assert.equal(label(scan({ active: true, goal: CHECK }), DISCOVER), "■ Stop check scan");
  assert.equal(label(scan({ active: true }), CHECK), "■ Stop discover scan");
});

test("the scan button stops a running scan or capture, and is disabled during a probe or restore", () => {
  assert.deepEqual(scanButton(scan({ active: true, from: null, to: null, ids: [5, 9] }), null, DISCOVER), {
    label: "■ Stop discover scan",
    disabled: false,
  });
  assert.deepEqual(scanButton(scan({}), probe({}), DISCOVER), { label: "▶ Start discover scan", disabled: true });
  assert.deepEqual(scanButton(scan({}), probe({ kind: "restore" }), CHECK), {
    label: "▶ Start check scan",
    disabled: true,
  });
  assert.equal(scanButton(scan({}), probe({ active: false }), DISCOVER).disabled, false);
  assert.equal(scanButton(null, null, DISCOVER).disabled, false);
});

test("the device runs something while a scan or a probe is active", () => {
  assert.equal(isRunning(scan({ active: true }), null), true);
  assert.equal(isRunning(scan({}), probe({})), true);
  assert.equal(isRunning(scan({ done: true }), probe({ active: false })), false);
  assert.equal(isRunning(null, null), false);
});

test("what runs names a scan's goal, a list run's size, or the probed ID", () => {
  assert.equal(runningText(CATALOGUE, scan({ active: true }), null), "discover scan");
  assert.equal(runningText(CATALOGUE, scan({ active: true, goal: CHECK, from: null, to: null }), null), "check scan");
  assert.equal(
    runningText(CATALOGUE, scan({ active: true, from: null, to: null, ids: [5, 9] }), null),
    "discover scan of 2 listed IDs",
  );
  assert.equal(runningText(CATALOGUE, scan({}), probe({})), `write probe of ID ${CANDIDATE_ID}`);
  assert.equal(runningText(CATALOGUE, scan({}), probe({ kind: "restore" })), `restore of ID ${CANDIDATE_ID}`);
  assert.equal(runningText(CATALOGUE, scan({}), probe({ active: false })), null);
});

test("a start refused with 409 shows what runs from the reply", () => {
  const body = (scanState, probeState) => JSON.stringify({ error: "busy", scan: scanState, probe: probeState });

  assert.equal(busyText(CATALOGUE, body(scan({ active: true, goal: CHECK }), null)), "busy: check scan");
  assert.equal(busyText(CATALOGUE, body(scan({ done: true }), probe({}))), `busy: write probe of ID ${CANDIDATE_ID}`);
});

test("a 409 whose reply names nothing running still says busy", () => {
  for (const body of ["", "busy", "{}", JSON.stringify({ error: "busy", scan: scan({}), probe: null })]) {
    assert.equal(busyText(CATALOGUE, body), `busy: ${BUSY_UNKNOWN}`, body);
  }
});

test("switching tabs stops only a running scan of another goal", () => {
  assert.equal(switchStopsScan(scan({ active: true, goal: DISCOVER }), CHECK), true);
  assert.equal(switchStopsScan(scan({ active: true, goal: CHECK }), DISCOVER), true);
  assert.equal(switchStopsScan(scan({ active: true, goal: CHECK }), CHECK), false);
  assert.equal(switchStopsScan(scan({ active: false, goal: DISCOVER }), CHECK), false);
  assert.equal(switchStopsScan(null, CHECK), false);
});

test("a refused start shows the device's error text", () => {
  const error = "from and to must both be given, 0-255, with from <= to";
  assert.equal(rangeError(JSON.stringify({ error })), error);
});

test("a refused start whose body has no error text shows the body, or says the range was refused", () => {
  assert.equal(rangeError("Bad Request"), "Bad Request");
  assert.equal(rangeError(JSON.stringify({ error: 5 })), JSON.stringify({ error: 5 }));
  assert.equal(rangeError("  "), RANGE_REFUSED);
  assert.equal(rangeError(""), RANGE_REFUSED);
});

test("a refused probe or restore shows the device's error text, else the body, else says the value was refused", () => {
  const error = "value too long";
  assert.equal(probeError(JSON.stringify({ error })), error);
  assert.equal(probeError("Bad Request"), "Bad Request");
  assert.equal(probeError("  "), PROBE_REFUSED);
  assert.equal(probeError(""), PROBE_REFUSED);
});

test("a start or stop reply gives the scan object it carries", () => {
  const reply = scan({ active: true, from: 3, to: 200, next: 3 });
  const idleCheck = scan({ goal: CHECK, from: null, to: null });
  const listRun = scan({ active: true, from: null, to: null, ids: [5, 12], next: 5 });

  assert.deepEqual(scanReply(JSON.stringify(reply)), reply);
  assert.deepEqual(scanReply(JSON.stringify(idleCheck)), idleCheck);
  assert.deepEqual(scanReply(JSON.stringify(listRun)), listRun);
});

// The device accepted the request; a body the page cannot read only means it learns the scan from the next poll.
test("a reply body that is no scan object gives none", () => {
  for (const body of ["", "OK", "{not json", "null", "5", "[]", "{}", '{"active": "yes", "goal": "discover"}',
    '{"active": true, "goal": 5}']) {
    assert.equal(scanReply(body), null, body);
  }
});

// The progress labels read every field: a reply missing one would show "Scanning ID undefined".
test("a reply body that is not a full scan object gives none", () => {
  const full = scan({ active: true, from: 3, to: 200, next: 3 });
  const broken = [
    ...["from", "to", "ids", "next", "done"].map((key) => ({ ...full, [key]: undefined })),
    { ...full, ids: "5,12" },
    { ...full, ids: [5, "12"] },
    { ...full, from: "3" },
    { ...full, to: 2.5 },
    { ...full, next: "3" },
    { ...full, done: "false" },
    { ...full, done: null },
  ];
  for (const reply of broken) {
    const body = JSON.stringify(reply);
    assert.equal(scanReply(body), null, body);
  }
});

test("an accepted discover start's reply gives the range to remember", () => {
  assert.deepEqual(acceptedRange(CATALOGUE, scan({ active: true, from: 3, to: 200 }), "3", "200"), {
    from: 3,
    to: 200,
  });
});

// The device decides how a sent range reads: only the range it runs is remembered.
test("a reply that runs another range than the one sent gives none", () => {
  assert.equal(acceptedRange(CATALOGUE, scan({ active: true, from: 1, to: 255 }), "3", "200"), null);
  assert.equal(acceptedRange(CATALOGUE, scan({ active: true, from: 3, to: 255 }), "3", "200"), null);
  assert.equal(acceptedRange(CATALOGUE, scan({ active: true, from: 1, to: 200 }), "3", "200"), null);
  // Both missing makes the device run its default range, which is still not the range sent.
  assert.equal(acceptedRange(CATALOGUE, scan({ active: true, from: 1, to: 255 }), "", ""), null);
});

test("a range typed with leading zeros is the same range", () => {
  assert.deepEqual(acceptedRange(CATALOGUE, scan({ active: true, from: 3, to: 20 }), "003", "020"), {
    from: 3,
    to: 20,
  });
});

// A check or a list run has no range to remember.
test("a reply without a discover range gives none", () => {
  assert.equal(acceptedRange(CATALOGUE, scan({ active: true, goal: CHECK, from: null, to: null }), "1", "255"), null);
  assert.equal(acceptedRange(CATALOGUE, scan({ active: true, goal: CHECK, from: 1, to: 255 }), "1", "255"), null);
  assert.equal(acceptedRange(CATALOGUE, scan({ active: true, from: null, to: null }), "1", "255"), null);
  assert.equal(acceptedRange(CATALOGUE, scan({ active: true, from: null, to: null, ids: [1] }), "1", "255"), null);
  assert.equal(acceptedRange(CATALOGUE, scan({ active: true, from: "3", to: 200 }), "3", "200"), null);
  assert.equal(acceptedRange(CATALOGUE, null, "1", "255"), null);
});
