import { test } from "node:test";
import assert from "node:assert/strict";

import { exportCsv, exportJson } from "./export.mjs";
import { markNow } from "./mark.mjs";
import { applyResponse, emptyCopy, fromStored, toStored } from "./records.mjs";
import { FIXTURE_CATALOGUE } from "./fixture_catalogue.mjs";

const BROWSER_NOW_MS = Date.UTC(2026, 10, 21, 12, 0, 0);
const SCAN = { active: false, goal: "discover", from: 1, to: 208, next: 1, done: true };
const [LEVEL, OTHER_LEVEL] = FIXTURE_CATALOGUE.levelIds;
const DATE = FIXTURE_CATALOGUE.dateId;

/**
 * /sniffer/data as the device sends it: `\u00XX` per control or non-ASCII byte. 5 and 9 are unknown; of the known
 * ones, the level LEVEL (10) matches, OTHER_LEVEL (11) does not, and DATE (31) only ever failed.
 */
const DEVICE_JSON = String.raw`{
  "mac": "aabbccddeeff", "name": "ventilation", "boot": "9f2c41ab", "uptime": 10000,
  "seq": 4, "full": true, "dropped": 0,
  "scan": {"active": false, "goal": "discover", "from": 1, "to": 208, "next": 1, "done": true},
  "records": [
    {"id": 5, "cmd": 32, "raw": "0080", "first": 1000, "last": 9000, "changed": 7000,
     "changes": 1, "status": "ok", "status_at": 1000, "reg": false,
     "hist": [[7000, "0080"], [1000, "0100"]]},
    {"id": 9, "cmd": 32, "raw": "\u0001Aé, \"x\"", "first": 2000, "last": 2000, "changed": 2000,
     "changes": 0, "status": "unqueried", "status_at": null, "reg": false,
     "hist": [[2000, "\u0001Aé, \"x\""]]},
    {"id": 10, "cmd": 32, "raw": "7", "first": 2000, "last": 2000, "changed": 2000,
     "changes": 0, "status": "ok", "status_at": 2000, "reg": true, "hist": [[2000, "7"]]},
    {"id": 11, "cmd": 32, "raw": "99 ", "first": 2000, "last": 2000, "changed": 2000,
     "changes": 0, "status": "ok", "status_at": 2000, "reg": false, "hist": [[2000, "99 "]]},
    {"id": 31, "cmd": null, "raw": null, "first": null, "last": null, "changed": null,
     "changes": 0, "status": "nak", "status_at": 3000, "reg": false, "hist": []}
  ]
}`;

function copyFromDevice() {
  return applyResponse(emptyCopy(), JSON.parse(DEVICE_JSON), BROWSER_NOW_MS).copy;
}

/** After the mark: 5 changes, the date gets its first value, 40 is new. */
function changedAfterMark(copy) {
  const record = (id, raw) => ({
    id, cmd: 32, raw, first: 11_000, last: 11_000, changed: 11_000,
    changes: 1, status: "ok", status_at: 11_000, reg: false, hist: [[11_000, raw]],
  });
  const delta = { ...JSON.parse(DEVICE_JSON), uptime: 12_000, seq: 9, full: false,
    records: [record(5, "0090"), record(DATE, "21.11.26"), record(40, "1")] };
  return applyResponse(copy, delta, BROWSER_NOW_MS + 2000).copy;
}

const iso = (uptimeMs) => new Date(BROWSER_NOW_MS - (10_000 - uptimeMs)).toISOString();
const byId = (exported, id) => exported.records.find((record) => record.id === id);

test("the JSON export holds the device, its uptime and scan at export time, and every record with its note", () => {
  const exported = JSON.parse(
    exportJson(FIXTURE_CATALOGUE, copyFromDevice(), { 5: "Fan level?" }, null, BROWSER_NOW_MS + 2000),
  );

  assert.deepEqual(exported.device, { mac: "aabbccddeeff", name: "ventilation", boot: "9f2c41ab" });
  assert.equal(exported.uptime_ms, 12_000);
  assert.deepEqual(exported.scan, SCAN);
  assert.deepEqual(exported.records[0], {
    id: 5,
    cmd: 32,
    raw: "0080",
    guessed_type: "padded int",
    registered: false,
    known: false,
    meaning: null,
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
    mark_raw: null,
  });
});

test("a stored copy the device has not confirmed this page load exports no uptime", () => {
  const reloaded = fromStored(JSON.parse(JSON.stringify(toStored(copyFromDevice()))));

  const exported = JSON.parse(exportJson(FIXTURE_CATALOGUE, reloaded, {}, null, BROWSER_NOW_MS + 2000));

  assert.deepEqual(exported.device, { mac: "aabbccddeeff", name: "ventilation", boot: "9f2c41ab" });
  assert.equal(exported.uptime_ms, null);
});

test("a record that only ever failed exports with null value and times", () => {
  const exported = JSON.parse(exportJson(FIXTURE_CATALOGUE, copyFromDevice(), {}, null, BROWSER_NOW_MS));

  const failed = byId(exported, DATE);
  assert.equal(failed.raw, null);
  assert.equal(failed.guessed_type, null);
  assert.deepEqual(failed.first_seen, { uptime_ms: null, wall: null });
  assert.deepEqual(failed.history, []);
  assert.equal(failed.note, "");
});

test("control and non-ASCII bytes round-trip byte-exact from the device JSON into the JSON export", () => {
  const exported = JSON.parse(exportJson(FIXTURE_CATALOGUE, copyFromDevice(), {}, null, BROWSER_NOW_MS));

  const raw = byId(exported, 9).raw;
  assert.deepEqual(
    [...raw].map((c) => c.charCodeAt(0)),
    [0x01, 0x41, 0xe9, 0x2c, 0x20, 0x22, 0x78, 0x22],
  );
});

test("the JSON export says which records are known and what a known value means", () => {
  const exported = JSON.parse(exportJson(FIXTURE_CATALOGUE, copyFromDevice(), {}, null, BROWSER_NOW_MS));

  const knownAndMeaning = (id) => [byId(exported, id).known, byId(exported, id).meaning];
  assert.deepEqual(knownAndMeaning(5), [false, null]);
  assert.deepEqual(knownAndMeaning(LEVEL), [true, "Burst"]);
  assert.deepEqual(knownAndMeaning(OTHER_LEVEL), [true, "unexpected value 99"]);
  assert.deepEqual(knownAndMeaning(DATE), [true, null]);
});

test("without a mark the JSON export has a null mark and no mark values", () => {
  const exported = JSON.parse(exportJson(FIXTURE_CATALOGUE, copyFromDevice(), {}, null, BROWSER_NOW_MS));

  assert.equal(exported.mark, null);
  assert.deepEqual(
    exported.records.map((record) => record.mark_raw),
    [null, null, null, null, null],
  );
});

test("with a mark the JSON export holds when it was set and each record's raw value then", () => {
  const marked = copyFromDevice();
  const mark = markNow(marked, BROWSER_NOW_MS + 1000);

  const exported = JSON.parse(exportJson(FIXTURE_CATALOGUE, changedAfterMark(marked), {}, mark, BROWSER_NOW_MS + 3000));

  assert.deepEqual(exported.mark, { set_at: new Date(BROWSER_NOW_MS + 1000).toISOString(), uptime_ms: 11_000 });
  assert.equal(byId(exported, 5).raw, "0090");
  assert.equal(byId(exported, 5).mark_raw, "0080");
  assert.equal(byId(exported, OTHER_LEVEL).mark_raw, "99 ");
  assert.equal(byId(exported, DATE).mark_raw, null);
  assert.equal(byId(exported, 40).mark_raw, null);
  assert.equal(byId(exported, 9).mark_raw, byId(exported, 9).raw);
});

test("the CSV export has one row per ID in ID order, raw value, meaning and note quoted", () => {
  const csv = exportCsv(FIXTURE_CATALOGUE, copyFromDevice(), { 9: 'says "hi"' }, null);

  assert.deepEqual(csv.split("\r\n"), [
    "id,raw,guessed_type,registered,known,meaning,status,change_count,first_seen,last_changed,note,mark_raw",
    `5,"0080",padded int,false,false,"",ok,1,${iso(1000)},${iso(7000)},"",""`,
    `9,"\u0001Aé, ""x""",text,false,false,"",unqueried,0,${iso(2000)},${iso(2000)},"says ""hi""",""`,
    `10,"7",int,true,true,"Burst",ok,0,${iso(2000)},${iso(2000)},"",""`,
    `11,"99 ",int,false,true,"unexpected value 99",ok,0,${iso(2000)},${iso(2000)},"",""`,
    `31,"",,false,true,"",nak,0,,,"",""`,
  ]);
});

test("with a mark the CSV export ends each row with the raw value at the mark, quoted", () => {
  const marked = copyFromDevice();
  const mark = markNow(marked, BROWSER_NOW_MS + 1000);

  const rows = exportCsv(FIXTURE_CATALOGUE, changedAfterMark(marked), {}, mark).split("\r\n");

  const markRawOf = (id) => rows.find((line) => line.startsWith(`${id},`)).split(",").at(-1);
  assert.equal(markRawOf(5), '"0080"');
  assert.equal(markRawOf(OTHER_LEVEL), '"99 "');
  assert.equal(markRawOf(DATE), '""');
  assert.equal(markRawOf(40), '""');
});
