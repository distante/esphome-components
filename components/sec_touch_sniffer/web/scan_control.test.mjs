import { test } from "node:test";
import assert from "node:assert/strict";

import { acceptedRange, GOAL_CHECK, GOAL_DISCOVER, rangeError, scanButtonLabel, scanStartPath, switchStopsScan } from "./scan_control.mjs";

const scan = (fields) => ({ active: false, goal: GOAL_DISCOVER, from: 1, to: 255, next: null, done: false, ...fields });

test("a discover start sends the goal and the range as typed, URL-encoded", () => {
  assert.equal(scanStartPath(GOAL_DISCOVER, "3", "200"), "/sniffer/scan/start?goal=discover&from=3&to=200");
  assert.equal(scanStartPath(GOAL_DISCOVER, " 1&x", "2 0"), "/sniffer/scan/start?goal=discover&from=%201%26x&to=2%200");
  assert.equal(scanStartPath(GOAL_DISCOVER, "", "999"), "/sniffer/scan/start?goal=discover&from=&to=999");
});

test("a check start sends only the goal", () => {
  assert.equal(scanStartPath(GOAL_CHECK, "3", "200"), "/sniffer/scan/start?goal=check");
});

test("the start button names the open tab's goal", () => {
  assert.equal(scanButtonLabel(scan({}), GOAL_DISCOVER), "▶ Start discover scan");
  assert.equal(scanButtonLabel(scan({ goal: GOAL_DISCOVER, done: true }), GOAL_CHECK), "▶ Start check scan");
});

test("the stop button names the running scan's goal only when it is not the open tab's", () => {
  assert.equal(scanButtonLabel(scan({ active: true }), GOAL_DISCOVER), "■ Stop scan");
  assert.equal(scanButtonLabel(scan({ active: true, goal: GOAL_CHECK }), GOAL_DISCOVER), "■ Stop check scan");
  assert.equal(scanButtonLabel(scan({ active: true }), GOAL_CHECK), "■ Stop discover scan");
});

test("switching tabs stops only a running scan of another goal", () => {
  assert.equal(switchStopsScan(scan({ active: true, goal: GOAL_DISCOVER }), GOAL_CHECK), true);
  assert.equal(switchStopsScan(scan({ active: true, goal: GOAL_CHECK }), GOAL_DISCOVER), true);
  assert.equal(switchStopsScan(scan({ active: true, goal: GOAL_CHECK }), GOAL_CHECK), false);
  assert.equal(switchStopsScan(scan({ active: false, goal: GOAL_DISCOVER }), GOAL_CHECK), false);
  assert.equal(switchStopsScan(null, GOAL_CHECK), false);
});

test("a refused start shows the device's error text", () => {
  const error = "from and to must both be given, 0-255, with from <= to";
  assert.equal(rangeError(JSON.stringify({ error })), error);
});

test("a refused start whose body has no error text shows the body, or says the range was refused", () => {
  assert.equal(rangeError("Bad Request"), "Bad Request");
  assert.equal(rangeError(JSON.stringify({ error: 5 })), JSON.stringify({ error: 5 }));
  assert.equal(rangeError("  "), "the device refused the range");
  assert.equal(rangeError(""), "the device refused the range");
});

test("an accepted discover start's reply gives the range to remember", () => {
  assert.deepEqual(acceptedRange(scan({ active: true, from: 3, to: 200 })), { from: 3, to: 200 });
});

// A start while a check runs is a no-op that replies with the running check: there is no range to remember.
test("a reply without a discover range gives none", () => {
  assert.equal(acceptedRange(scan({ active: true, goal: GOAL_CHECK, from: null, to: null })), null);
  assert.equal(acceptedRange(scan({ active: true, goal: GOAL_CHECK, from: 1, to: 255 })), null);
  assert.equal(acceptedRange(scan({ active: true, from: null, to: null })), null);
  assert.equal(acceptedRange(scan({ active: true, from: "3", to: 200 })), null);
  assert.equal(acceptedRange(null), null);
});
