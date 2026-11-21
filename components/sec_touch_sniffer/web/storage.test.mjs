import { test } from "node:test";
import assert from "node:assert/strict";

import { applyResponse, emptyCopy } from "./records.mjs";
import { deviceKey, LAST_MAC_KEY, parseStored, STORAGE_PREFIX, storedText } from "./storage.mjs";

const BROWSER_NOW_MS = Date.UTC(2026, 10, 21, 12, 0, 0);

function deviceCopy() {
  const record = {
    id: 5,
    cmd: 32,
    raw: "0100",
    first: 1000,
    last: 5000,
    changed: 3000,
    changes: 0,
    status: "ok",
    status_at: 1000,
    reg: false,
    hist: [[3000, "0100"]],
  };
  const response = {
    mac: "aabbccddeeff",
    name: "ventilation",
    boot: "9f2c41ab",
    uptime: 10_000,
    seq: 7,
    full: true,
    dropped: 0,
    scan: null,
    records: [record],
  };
  return applyResponse(emptyCopy(), response, BROWSER_NOW_MS).copy;
}

const nothing = () => ({ copy: emptyCopy(), notes: {}, mutes: new Set() });

test("every key shares the storage prefix", () => {
  assert.ok(LAST_MAC_KEY.startsWith(STORAGE_PREFIX));
  assert.equal(deviceKey("aabbccddeeff"), `${STORAGE_PREFIX}aabbccddeeff`);
  assert.notEqual(deviceKey("aabbccddeeff"), LAST_MAC_KEY);
});

test("a stored device reloads with its copy, notes and mutes", () => {
  const stored = parseStored(storedText(deviceCopy(), { 5: "Fan level?" }, new Set([5, 9])));

  assert.equal(stored.copy.records[5].raw, "0100");
  assert.equal(stored.copy.since, null);
  assert.deepEqual(stored.notes, { 5: "Fan level?" });
  assert.deepEqual(stored.mutes, new Set([5, 9]));
});

test("nothing stored, or text that is not JSON, gives nothing", () => {
  assert.deepEqual(parseStored(null), nothing());
  assert.deepEqual(parseStored("{not json"), nothing());
  assert.deepEqual(parseStored("5"), nothing());
  assert.deepEqual(parseStored("[1, 2]"), nothing());
});

test("notes keep only text values keyed by an ID", () => {
  const text = JSON.stringify({ notes: { 5: "Fan level?", 6: 7, 7: null, 8: { a: 1 }, x: "not an ID" } });

  assert.deepEqual(parseStored(text).notes, { 5: "Fan level?" });
  assert.deepEqual(parseStored(JSON.stringify({ notes: "nope" })).notes, {});
  assert.deepEqual(parseStored(JSON.stringify({ notes: ["a"] })).notes, {});
});

test("mutes that are not an array of integer IDs are dropped", () => {
  for (const mutes of [5, "5", null, { 5: true }, [5, "6"], [1.5]]) {
    assert.deepEqual(parseStored(JSON.stringify({ mutes })).mutes, new Set(), JSON.stringify(mutes));
  }
});

test("a corrupt copy gives an empty copy but keeps the notes and mutes", () => {
  const stored = parseStored(JSON.stringify({ copy: "nope", notes: { 5: "x" }, mutes: [5] }));

  assert.deepEqual(stored.copy, emptyCopy());
  assert.deepEqual(stored.notes, { 5: "x" });
  assert.deepEqual(stored.mutes, new Set([5]));
});
