import { test } from "node:test";
import assert from "node:assert/strict";

import { applyResponse, changeFeed, emptyCopy, fromStored, toStored, wallOf } from "./records.mjs";

const BROWSER_NOW_MS = Date.UTC(2026, 10, 21, 12, 0, 0);

/** A record as /sniffer/data serves it (.specs/sniffer.md, "JSON shape"). */
function deviceRecord(id, raw, overrides = {}) {
  return {
    id,
    cmd: 32,
    raw,
    first: 1000,
    last: 5000,
    changed: 3000,
    changes: 0,
    status: "unqueried",
    status_at: null,
    reg: false,
    hist: [[3000, raw]],
    ...overrides,
  };
}

function response(records, overrides = {}) {
  return {
    mac: "aabbccddeeff",
    name: "ventilation",
    boot: "9f2c41ab",
    uptime: 10_000,
    seq: 7,
    full: true,
    dropped: 0,
    scan: null,
    records,
    ...overrides,
  };
}

test("a full response fills an empty copy and remembers boot and seq", () => {
  const { copy } = applyResponse(emptyCopy(), response([deviceRecord(5, "0100")]), BROWSER_NOW_MS);

  assert.equal(copy.boot, "9f2c41ab");
  assert.equal(copy.since, 7);
  assert.equal(copy.records[5].raw, "0100");
  assert.equal(copy.records[5].stale, false);
});

test("a delta replaces only the records it carries, history included", () => {
  const first = applyResponse(
    emptyCopy(),
    response([deviceRecord(5, "0100"), deviceRecord(6, "1")]),
    BROWSER_NOW_MS,
  ).copy;
  const changed = deviceRecord(5, "0080", {
    changes: 1,
    hist: [
      [8000, "0080"],
      [3000, "0100"],
    ],
  });

  const { copy } = applyResponse(first, response([changed], { full: false, seq: 9 }), BROWSER_NOW_MS);

  assert.equal(copy.since, 9);
  assert.deepEqual(copy.records[5].hist, [
    [8000, "0080"],
    [3000, "0100"],
  ]);
  assert.equal(copy.records[6].raw, "1");
});

test("after a reboot the device wins for its IDs and browser-only IDs stay as stale", () => {
  const before = applyResponse(
    emptyCopy(),
    response([deviceRecord(5, "0100"), deviceRecord(6, "1")]),
    BROWSER_NOW_MS,
  ).copy;

  const { copy } = applyResponse(
    before,
    response([deviceRecord(5, "0200")], { boot: "00000001", seq: 2 }),
    BROWSER_NOW_MS,
  );

  assert.equal(copy.boot, "00000001");
  assert.equal(copy.records[5].raw, "0200");
  assert.equal(copy.records[5].stale, false);
  assert.equal(copy.records[6].raw, "1");
  assert.equal(copy.records[6].stale, true);
});

test("a stale ID turns live again when the device sees it", () => {
  const stale = applyResponse(
    applyResponse(emptyCopy(), response([deviceRecord(6, "1")]), BROWSER_NOW_MS).copy,
    response([], { boot: "00000001", seq: 1 }),
    BROWSER_NOW_MS,
  ).copy;

  const { copy } = applyResponse(stale, response([deviceRecord(6, "2")], { boot: "00000001", full: false, seq: 3 }), BROWSER_NOW_MS);

  assert.equal(copy.records[6].stale, false);
});

test("the IDs whose raw value changed are reported, so the page can flash them", () => {
  const first = applyResponse(
    emptyCopy(),
    response([deviceRecord(5, "0100"), deviceRecord(6, "1")]),
    BROWSER_NOW_MS,
  ).copy;

  const { changedIds } = applyResponse(
    first,
    response([deviceRecord(5, "0080"), deviceRecord(6, "1"), deviceRecord(7, "3")], { full: false, seq: 9 }),
    BROWSER_NOW_MS,
  );

  assert.deepEqual(changedIds, [5]);
});

test("a record's times read as wall time anchored at the response that carried it", () => {
  const { copy } = applyResponse(emptyCopy(), response([deviceRecord(5, "0100")], { uptime: 10_000 }), BROWSER_NOW_MS);

  assert.equal(wallOf(copy.records[5], 3000), BROWSER_NOW_MS - 7000);
  assert.equal(wallOf(copy.records[5], null), null);
});

test("a stored copy reloads with its records and boot but polls from scratch", () => {
  const { copy } = applyResponse(emptyCopy(), response([deviceRecord(5, "0100")]), BROWSER_NOW_MS);

  const reloaded = fromStored(JSON.parse(JSON.stringify(toStored(copy))));

  assert.equal(reloaded.boot, "9f2c41ab");
  assert.equal(reloaded.records[5].raw, "0100");
  assert.equal(reloaded.since, null);
});

test("unreadable storage gives an empty copy", () => {
  assert.deepEqual(fromStored(null), emptyCopy());
  assert.deepEqual(fromStored({ records: "nope" }), emptyCopy());
});

test("the change feed lists changes across IDs newest first, without muted IDs", () => {
  const { copy } = applyResponse(
    emptyCopy(),
    response(
      [
        deviceRecord(2, "13:32:09", {
          hist: [
            [9000, "13:32:09"],
            [8000, "13:32:08"],
          ],
        }),
        deviceRecord(5, "0080", {
          hist: [
            [7000, "0080"],
            [4000, "0100"],
            [1000, "0000"],
          ],
        }),
      ],
      { uptime: 10_000 },
    ),
    BROWSER_NOW_MS,
  );

  const feed = changeFeed(copy, new Set([2]));

  assert.deepEqual(feed, [
    { id: 5, atMs: BROWSER_NOW_MS - 3000, from: "0100", to: "0080" },
    { id: 5, atMs: BROWSER_NOW_MS - 6000, from: "0000", to: "0100" },
  ]);
});
