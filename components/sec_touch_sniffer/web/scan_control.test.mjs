import { test } from "node:test";
import assert from "node:assert/strict";

import {
  acceptedRange,
  RANGE_REFUSED,
  rangeError,
  scanButtonLabel,
  scanReply,
  scanStartPath,
  switchStopsScan,
} from "./scan_control.mjs";

// The part of catalogue.mjs's shape this module reads.
const CATALOGUE = { goals: { discover: "discover", check: "check" } };
const { discover: DISCOVER, check: CHECK } = CATALOGUE.goals;

const scan = (fields) => ({ active: false, goal: DISCOVER, from: 1, to: 255, next: null, done: false, ...fields });

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

test("the start button names the open tab's goal", () => {
  assert.equal(scanButtonLabel(scan({}), DISCOVER), "▶ Start discover scan");
  assert.equal(scanButtonLabel(scan({ goal: DISCOVER, done: true }), CHECK), "▶ Start check scan");
});

test("the stop button always names the running scan's goal", () => {
  assert.equal(scanButtonLabel(scan({ active: true }), DISCOVER), "■ Stop discover scan");
  assert.equal(scanButtonLabel(scan({ active: true, goal: CHECK }), CHECK), "■ Stop check scan");
  assert.equal(scanButtonLabel(scan({ active: true, goal: CHECK }), DISCOVER), "■ Stop check scan");
  assert.equal(scanButtonLabel(scan({ active: true }), CHECK), "■ Stop discover scan");
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

test("a start or stop reply gives the scan object it carries", () => {
  const reply = scan({ active: true, from: 3, to: 200, next: 3 });
  const idleCheck = scan({ goal: CHECK, from: null, to: null });

  assert.deepEqual(scanReply(JSON.stringify(reply)), reply);
  assert.deepEqual(scanReply(JSON.stringify(idleCheck)), idleCheck);
});

// The device accepted the request; a body the page cannot read only means it learns the scan from the next poll.
test("a reply body that is no scan object gives none", () => {
  for (const body of ["", "OK", "{not json", "null", "5", "[]", "{}", '{"active": "yes", "goal": "discover"}',
    '{"active": true, "goal": 5}']) {
    assert.equal(scanReply(body), null, body);
  }
});

test("an accepted discover start's reply gives the range to remember", () => {
  assert.deepEqual(acceptedRange(CATALOGUE, scan({ active: true, from: 3, to: 200 }), "3", "200"), { from: 3, to: 200 });
});

// A start while a scan runs is a no-op that replies with the running scan: its range is not the one sent.
test("a reply that runs another range than the one sent gives none", () => {
  assert.equal(acceptedRange(CATALOGUE, scan({ active: true, from: 1, to: 255 }), "3", "200"), null);
  assert.equal(acceptedRange(CATALOGUE, scan({ active: true, from: 3, to: 255 }), "3", "200"), null);
  assert.equal(acceptedRange(CATALOGUE, scan({ active: true, from: 1, to: 200 }), "3", "200"), null);
  // Both missing makes the device run its default range, which is still not the range sent.
  assert.equal(acceptedRange(CATALOGUE, scan({ active: true, from: 1, to: 255 }), "", ""), null);
});

test("a range typed with leading zeros is the same range", () => {
  assert.deepEqual(acceptedRange(CATALOGUE, scan({ active: true, from: 3, to: 20 }), "003", "020"), { from: 3, to: 20 });
});

// A start while a check runs is a no-op that replies with the running check: there is no range to remember.
test("a reply without a discover range gives none", () => {
  assert.equal(acceptedRange(CATALOGUE, scan({ active: true, goal: CHECK, from: null, to: null }), "1", "255"), null);
  assert.equal(acceptedRange(CATALOGUE, scan({ active: true, goal: CHECK, from: 1, to: 255 }), "1", "255"), null);
  assert.equal(acceptedRange(CATALOGUE, scan({ active: true, from: null, to: null }), "1", "255"), null);
  assert.equal(acceptedRange(CATALOGUE, scan({ active: true, from: "3", to: 200 }), "3", "200"), null);
  assert.equal(acceptedRange(CATALOGUE, null, "1", "255"), null);
});
