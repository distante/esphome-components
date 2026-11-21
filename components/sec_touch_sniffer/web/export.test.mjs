import { test } from "node:test";
import assert from "node:assert/strict";

import { exportCsv, exportJson } from "./export.mjs";
import { applyResponse, emptyCopy } from "./records.mjs";

const BROWSER_NOW_MS = Date.UTC(2026, 10, 21, 12, 0, 0);
const SCAN = { active: false, from: 1, to: 208, next: 1, done: true };

/** /sniffer/data as the device sends it: `\u00XX` per control or non-ASCII byte. */
const DEVICE_JSON = String.raw`{
  "mac": "aabbccddeeff", "name": "ventilation", "boot": "9f2c41ab", "uptime": 10000,
  "seq": 4, "full": true, "dropped": 0,
  "scan": {"active": false, "from": 1, "to": 208, "next": 1, "done": true},
  "records": [
    {"id": 5, "cmd": 32, "raw": "0080", "first": 1000, "last": 9000, "changed": 7000,
     "changes": 1, "status": "ok", "status_at": 1000, "reg": false,
     "hist": [[7000, "0080"], [1000, "0100"]]},
    {"id": 9, "cmd": 32, "raw": "\u0001Aé, \"x\"", "first": 2000, "last": 2000, "changed": 2000,
     "changes": 0, "status": "unqueried", "status_at": null, "reg": true,
     "hist": [[2000, "\u0001Aé, \"x\""]]},
    {"id": 31, "cmd": null, "raw": null, "first": null, "last": null, "changed": null,
     "changes": 0, "status": "nak", "status_at": 3000, "reg": false, "hist": []}
  ]
}`;

function copyFromDevice() {
  return applyResponse(emptyCopy(), JSON.parse(DEVICE_JSON), BROWSER_NOW_MS).copy;
}

const iso = (uptimeMs) => new Date(BROWSER_NOW_MS - (10_000 - uptimeMs)).toISOString();

test("the JSON export holds the device, its uptime and scan at export time, and every record with its note", () => {
  const exported = JSON.parse(exportJson(copyFromDevice(), { 5: "Fan level?" }, BROWSER_NOW_MS + 2000));

  assert.deepEqual(exported.device, { mac: "aabbccddeeff", name: "ventilation", boot: "9f2c41ab" });
  assert.equal(exported.uptime_ms, 12_000);
  assert.deepEqual(exported.scan, SCAN);
  assert.deepEqual(exported.records[0], {
    id: 5,
    cmd: 32,
    raw: "0080",
    guessed_type: "padded int",
    registered: false,
    status: "ok",
    change_count: 1,
    first_seen: { uptime_ms: 1000, wall: iso(1000) },
    last_changed: { uptime_ms: 7000, wall: iso(7000) },
    history: [
      { raw: "0080", uptime_ms: 7000, wall: iso(7000) },
      { raw: "0100", uptime_ms: 1000, wall: iso(1000) },
    ],
    stale: false,
    note: "Fan level?",
  });
});

test("a record that only ever failed exports with null value and times", () => {
  const exported = JSON.parse(exportJson(copyFromDevice(), {}, BROWSER_NOW_MS));

  const failed = exported.records.find((record) => record.id === 31);
  assert.equal(failed.raw, null);
  assert.equal(failed.guessed_type, null);
  assert.deepEqual(failed.first_seen, { uptime_ms: null, wall: null });
  assert.deepEqual(failed.history, []);
  assert.equal(failed.note, "");
});

test("control and non-ASCII bytes round-trip byte-exact from the device JSON into the JSON export", () => {
  const exported = JSON.parse(exportJson(copyFromDevice(), {}, BROWSER_NOW_MS));

  const raw = exported.records.find((record) => record.id === 9).raw;
  assert.deepEqual(
    [...raw].map((c) => c.charCodeAt(0)),
    [0x01, 0x41, 0xe9, 0x2c, 0x20, 0x22, 0x78, 0x22],
  );
});

test("the CSV export has one row per ID in ID order, raw value and note quoted", () => {
  const csv = exportCsv(copyFromDevice(), { 9: 'says "hi"' });

  assert.deepEqual(csv.split("\r\n"), [
    "id,raw,guessed_type,registered,status,change_count,first_seen,last_changed,note",
    `5,"0080",padded int,false,ok,1,${iso(1000)},${iso(7000)},""`,
    `9,"\u0001Aé, ""x""",text,true,unqueried,0,${iso(2000)},${iso(2000)},"says ""hi"""`,
    `31,"",,false,nak,0,,,""`,
  ]);
});
