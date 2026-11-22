import { test } from "node:test";
import assert from "node:assert/strict";

import { scanProgress } from "./scan_progress.mjs";

test("a running scan names the ID it queries next and the range", () => {
  assert.deepEqual(scanProgress({ active: true, from: 1, to: 100, next: 26, done: false }), {
    label: "Scanning ID 26 of 1–100",
    percent: 25,
  });
});

test("a stopped scan names the ID it stopped at", () => {
  assert.deepEqual(scanProgress({ active: false, from: 1, to: 100, next: 51, done: false }), {
    label: "Stopped at ID 51",
    percent: 50,
  });
});

test("a completed scan is complete and full", () => {
  assert.deepEqual(scanProgress({ active: false, from: 1, to: 100, next: 1, done: true }), {
    label: "Scan of 1–100 complete",
    percent: 100,
  });
});

test("before any run the scan is not scanned yet and empty", () => {
  assert.deepEqual(
    scanProgress({ active: false, goal: "discover", from: null, to: null, next: null, done: false }),
    { label: "Not scanned yet", percent: 0 },
  );
});

test("a scan querying its last ID is not yet full", () => {
  assert.equal(scanProgress({ active: true, from: 1, to: 4, next: 4, done: false }).percent, 75);
});
