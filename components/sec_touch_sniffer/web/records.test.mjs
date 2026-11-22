import { test } from "node:test";
import assert from "node:assert/strict";

import { applyResponse, changeFeed, emptyCopy, fromStored, rowsOf, toStored, wallOf, withScan } from "./records.mjs";

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

test("malformed stored records are dropped, the readable ones stay", () => {
  const { copy } = applyResponse(emptyCopy(), response([deviceRecord(5, "0100")]), BROWSER_NOW_MS);
  const stored = JSON.parse(JSON.stringify(toStored(copy)));
  const good = stored.records[5];
  stored.records = {
    5: good,
    6: "nope",
    7: null,
    8: { ...good, id: "8" },
    9: { ...good, id: 9, hist: "nope" },
    10: { ...good, id: 10, anchor: undefined },
    11: { ...good, id: 11, anchor: { browserNowMs: BROWSER_NOW_MS } },
    12: { ...good, id: 12, raw: 12 },
    13: { ...good, id: 13, status: null },
    14: { ...good, id: 14, changes: "1" },
    15: { ...good, id: 15, reg: "false" },
    16: { ...good, id: 16, hist: ["0100"] },
    17: { ...good, id: 17, hist: [[3000, 100]] },
    18: { ...good, id: 18, hist: [["3000", "0100"]] },
    19: { ...good, id: 19, raw: null, hist: [] },
    20: { ...good, id: 20, first: "1000" },
    21: { ...good, id: 21, last: "5000" },
    22: { ...good, id: 22, changed: "3000" },
    23: { ...good, id: 23, status_at: "4000" },
    24: { ...good, id: 24, first: null, last: null, changed: null, status_at: 4000, cmd: null, status: "ok" },
    25: { ...good, id: 25, cmd: "32" },
    26: { ...good, id: 26, status: "maybe" },
    27: { ...good, id: 99 },
  };
  // JSON turns NaN into null, but a hand-edited or older store can still hold one.
  stored.records[28] = { ...good, id: 28, first: Number.NaN };

  const reloaded = fromStored(stored);

  assert.deepEqual(Object.keys(reloaded.records), ["5", "19", "24"]);
  assert.deepEqual(
    rowsOf(reloaded, {}).map((row) => row.id),
    [5, 19, 24],
  );
});

test("a stored device whose mac or name is not a string is dropped", () => {
  const { copy } = applyResponse(emptyCopy(), response([]), BROWSER_NOW_MS);
  const stored = JSON.parse(JSON.stringify(toStored(copy)));

  assert.equal(fromStored({ ...stored, device: { ...stored.device, mac: 1 } }).device, null);
  assert.equal(fromStored({ ...stored, device: { ...stored.device, name: null } }).device, null);
  assert.equal(fromStored(stored).device.name, "ventilation");
});

test("a stored copy reloads without a scan state, so no stale scan shows as live", () => {
  const scan = { active: true, from: 1, to: 208, next: 40, done: false };
  const { copy } = applyResponse(emptyCopy(), response([], { scan }), BROWSER_NOW_MS);

  const reloaded = fromStored(JSON.parse(JSON.stringify(toStored(copy))));

  assert.equal(reloaded.device.mac, "aabbccddeeff");
  assert.equal(reloaded.device.scan, null);
});

test("a stored device without a readable anchor is dropped", () => {
  const stored = { boot: "9f2c41ab", device: { mac: "aabbccddeeff", uptimeMs: 1, browserNowMs: 2 }, records: {} };

  assert.equal(fromStored(stored).device, null);
});

test("the device and its records share the anchor of the response that carried them", () => {
  const { copy } = applyResponse(emptyCopy(), response([deviceRecord(5, "0100")], { uptime: 10_000 }), BROWSER_NOW_MS);

  assert.deepEqual(copy.device.anchor, { browserNowMs: BROWSER_NOW_MS, uptimeMs: 10_000 });
  assert.equal(copy.device.anchor, copy.records[5].anchor);
});

// A start or stop replies with the scan object: the scan control decides on it before the next poll arrives.
test("a start or stop reply replaces the device's scan and nothing else", () => {
  const running = { active: true, goal: "discover", from: 1, to: 255, next: 40, done: false };
  const stopped = { ...running, active: false };
  const { copy } = applyResponse(emptyCopy(), response([deviceRecord(5, "0100")], { scan: running }), BROWSER_NOW_MS);

  const updated = withScan(copy, stopped);

  assert.deepEqual(updated, { ...copy, device: { ...copy.device, scan: stopped } });
  assert.equal(copy.device.scan, running, "the copy passed in is left unchanged");
});

test("a reply before any device answered leaves the copy as it is", () => {
  const copy = emptyCopy();

  assert.equal(withScan(copy, { active: false, goal: "discover" }), copy);
});
