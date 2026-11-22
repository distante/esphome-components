import { test } from "node:test";
import assert from "node:assert/strict";

import { changedSinceMark, markChanges, markNow, markRaw } from "./mark.mjs";
import { applyResponse, emptyCopy, fromStored, toStored } from "./records.mjs";
import { FIXTURE_CATALOGUE } from "./fixture_catalogue.mjs";
import { deviceRecord, deviceResponse } from "./fixture_records.mjs";

const BROWSER_NOW_MS = Date.UTC(2026, 10, 21, 12, 0, 0);
const [LEVEL] = FIXTURE_CATALOGUE.levelIds;

/** Unknown IDs 5 and 6 with a value, unknown 7 that only ever failed, and a known level. */
function before() {
  const records = [deviceRecord(5, "0100"), deviceRecord(6, "1"), deviceRecord(7, null), deviceRecord(LEVEL, "3")];
  return applyResponse(emptyCopy(), deviceResponse(records), BROWSER_NOW_MS).copy;
}

/** A delta: 5 changes, 6 is sent again unchanged, 7 gets its first value, the level changes, 40 is new. */
function after(copy) {
  const records = [
    deviceRecord(5, "0200"),
    deviceRecord(6, "1"),
    deviceRecord(7, "9"),
    deviceRecord(LEVEL, "7"),
    deviceRecord(40, "x"),
  ];
  return applyResponse(copy, deviceResponse(records, { full: false }), BROWSER_NOW_MS + 5000).copy;
}

test("a mark holds every raw value, the wall time it was set and the device uptime then", () => {
  const mark = markNow(before(), BROWSER_NOW_MS + 2000);

  assert.equal(mark.setAtMs, BROWSER_NOW_MS + 2000);
  assert.equal(mark.uptimeMs, 12_000);
  assert.equal(markRaw(mark, 5), "0100");
  assert.equal(markRaw(mark, LEVEL), "3");
  assert.equal(markRaw(mark, 7), null);
  assert.equal(markRaw(mark, 40), null);
});

test("a mark set before the device answered on this page load has no uptime", () => {
  assert.equal(markNow(emptyCopy(), BROWSER_NOW_MS).uptimeMs, null);
  const reloaded = fromStored(JSON.parse(JSON.stringify(toStored(before()))));

  const mark = markNow(reloaded, BROWSER_NOW_MS);

  assert.equal(mark.uptimeMs, null);
  assert.equal(markRaw(mark, 5), "0100");
});

test("changed since the mark means the raw value differs, no value at the mark included", () => {
  const mark = markNow(before(), BROWSER_NOW_MS);

  assert.equal(changedSinceMark(mark, 5, "0200"), true);
  assert.equal(changedSinceMark(mark, 5, "0100"), false);
  assert.equal(changedSinceMark(mark, 7, "9"), true);
  assert.equal(changedSinceMark(mark, 7, null), false);
  assert.equal(changedSinceMark(mark, 40, "x"), true);
  // Padding is part of the raw value.
  assert.equal(changedSinceMark(mark, 5, "0100 "), true);
});

test("without a mark nothing has changed since it", () => {
  assert.equal(changedSinceMark(null, 5, "0200"), false);
  assert.equal(markRaw(null, 5), null);
  assert.deepEqual(markChanges(FIXTURE_CATALOGUE, after(before()), null), []);
});

test("the changes since the mark are the unknown IDs whose value differs, in ID order, mark value and now", () => {
  const copy = before();
  const mark = markNow(copy, BROWSER_NOW_MS);

  assert.deepEqual(markChanges(FIXTURE_CATALOGUE, after(copy), mark), [
    { id: 5, markRaw: "0100", raw: "0200" },
    { id: 7, markRaw: null, raw: "9" },
    { id: 40, markRaw: null, raw: "x" },
  ]);
});

test("a value changed and changed back is no change since the mark", () => {
  const copy = before();
  const mark = markNow(copy, BROWSER_NOW_MS);
  const back = applyResponse(after(copy), deviceResponse([deviceRecord(5, "0100")], { full: false }), BROWSER_NOW_MS + 9000).copy;

  assert.deepEqual(
    markChanges(FIXTURE_CATALOGUE, back, mark).map((change) => change.id),
    [7, 40],
  );
});
