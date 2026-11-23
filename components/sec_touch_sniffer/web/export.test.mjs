import { test } from "node:test";
import assert from "node:assert/strict";

import { exportCsv, exportJson } from "./export.mjs";
import { applyResponse, emptyCopy, fromStored, toStored, withProbe } from "./records.mjs";
import { FIXTURE_CATALOGUE } from "./fixture_catalogue.mjs";
import { experimentWith, NAK } from "./fixture_experiments.mjs";
import { deviceRecord, deviceResponse } from "./fixture_records.mjs";

const BROWSER_NOW_MS = Date.UTC(2026, 10, 21, 12, 0, 0);
const SCAN = { active: false, goal: "discover", from: 1, to: 208, ids: null, next: 1, done: true };
/** The export options with no mutes and no experiments. */
const NO_EXPERIMENTS = {
  mutes: new Set(),
  includeMuted: false,
  experiments: [],
  openExperimentKey: null,
  onlyOpen: false,
  pendingProbe: null,
};
const [LEVEL, OTHER_LEVEL] = FIXTURE_CATALOGUE.levelIds;
const DATE = FIXTURE_CATALOGUE.dateId;

/** Unknown ID 9 as the device sends it in /sniffer/data: `\u00XX` per control or non-ASCII byte. */
const ESCAPED_RECORD_JSON = String.raw`{"id": 9, "cmd": 32, "raw": "\u0001Aé, \"x\"", "first": 2000, "last": 2000,
  "changed": 2000, "changes": 0, "status": "unqueried", "status_at": null, "reg": false,
  "hist": [[2000, "\u0001Aé, \"x\""]]}`;

/** 5 and 9 are unknown; of the known ones, the level LEVEL matches, OTHER_LEVEL does not, and DATE only ever failed. */
function copyFromDevice() {
  const records = [
    deviceRecord(5, "0080", { last: 9000, changed: 7000, changes: 1, hist: [[7000, "0080"], [1000, "0100"]] }),
    JSON.parse(ESCAPED_RECORD_JSON),
    deviceRecord(LEVEL, "7", { seenAt: 2000, reg: true }),
    deviceRecord(OTHER_LEVEL, "99 ", { seenAt: 2000 }),
    deviceRecord(DATE, null, { seenAt: 3000 }),
  ];
  return applyResponse(emptyCopy(), deviceResponse(records, { seq: 4, scan: SCAN }), BROWSER_NOW_MS).copy;
}

const iso = (uptimeMs) => new Date(BROWSER_NOW_MS - (10_000 - uptimeMs)).toISOString();
const byId = (exported, id) => exported.records.find((record) => record.id === id);

test("the JSON export holds the device, its uptime and scan at export time, and every record with its note", () => {
  const exported = JSON.parse(
    exportJson(FIXTURE_CATALOGUE, copyFromDevice(), { 5: "Fan level?" }, NO_EXPERIMENTS, BROWSER_NOW_MS + 2000),
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
  });
});

test("a stored copy the device has not confirmed this page load exports no uptime", () => {
  const reloaded = fromStored(JSON.parse(JSON.stringify(toStored(copyFromDevice()))));

  const exported = JSON.parse(exportJson(FIXTURE_CATALOGUE, reloaded, {}, NO_EXPERIMENTS, BROWSER_NOW_MS + 2000));

  assert.deepEqual(exported.device, { mac: "aabbccddeeff", name: "ventilation", boot: "9f2c41ab" });
  assert.equal(exported.uptime_ms, null);
});

test("a record that only ever failed exports with null value and times", () => {
  const exported = JSON.parse(exportJson(FIXTURE_CATALOGUE, copyFromDevice(), {}, NO_EXPERIMENTS, BROWSER_NOW_MS));

  const failed = byId(exported, DATE);
  assert.equal(failed.raw, null);
  assert.equal(failed.guessed_type, null);
  assert.deepEqual(failed.first_seen, { uptime_ms: null, wall: null });
  assert.deepEqual(failed.history, []);
  assert.equal(failed.note, "");
});

test("control and non-ASCII bytes round-trip byte-exact from the device JSON into the JSON export", () => {
  const exported = JSON.parse(exportJson(FIXTURE_CATALOGUE, copyFromDevice(), {}, NO_EXPERIMENTS, BROWSER_NOW_MS));

  const raw = byId(exported, 9).raw;
  assert.deepEqual(
    [...raw].map((c) => c.charCodeAt(0)),
    [0x01, 0x41, 0xe9, 0x2c, 0x20, 0x22, 0x78, 0x22],
  );
});

test("the JSON export says which records are known and what a known value means", () => {
  const exported = JSON.parse(exportJson(FIXTURE_CATALOGUE, copyFromDevice(), {}, NO_EXPERIMENTS, BROWSER_NOW_MS));

  const knownAndMeaning = (id) => [byId(exported, id).known, byId(exported, id).meaning];
  assert.deepEqual(knownAndMeaning(5), [false, null]);
  assert.deepEqual(knownAndMeaning(LEVEL), [true, "Burst"]);
  assert.deepEqual(knownAndMeaning(OTHER_LEVEL), [true, "unexpected value 99"]);
  assert.deepEqual(knownAndMeaning(DATE), [true, null]);
});

test("the CSV export has one row per ID in ID order, raw value, meaning and note quoted", () => {
  const csv = exportCsv(FIXTURE_CATALOGUE, copyFromDevice(), { 9: 'says "hi"' }, NO_EXPERIMENTS);

  assert.deepEqual(csv.split("\r\n"), [
    "id,raw,guessed_type,registered,known,meaning,status,change_count,first_seen,last_changed,note",
    `5,"0080",padded int,false,false,"",ok,1,${iso(1000)},${iso(7000)},""`,
    `9,"\u0001Aé, ""x""",text,false,false,"",unqueried,0,${iso(2000)},${iso(2000)},"says ""hi"""`,
    `${LEVEL},"7",int,true,true,"Burst",ok,0,${iso(2000)},${iso(2000)},""`,
    `${OTHER_LEVEL},"99 ",int,false,true,"unexpected value 99",ok,0,${iso(2000)},${iso(2000)},""`,
    `${DATE},"",,false,true,"",nak,0,,,""`,
  ]);
});

// ───── mutes ─────

const MUTED_ID = 9;
const withMutes = (includeMuted) => ({ ...NO_EXPERIMENTS, mutes: new Set([MUTED_ID]), includeMuted });

test("muted IDs are left out of the records unless asked for", () => {
  const exported = JSON.parse(exportJson(FIXTURE_CATALOGUE, copyFromDevice(), {}, withMutes(false), BROWSER_NOW_MS));

  assert.equal(byId(exported, MUTED_ID), undefined);
  assert.equal("muted" in byId(exported, 5), false);
});

test("with muted IDs included every record says whether it is muted", () => {
  const exported = JSON.parse(exportJson(FIXTURE_CATALOGUE, copyFromDevice(), {}, withMutes(true), BROWSER_NOW_MS));

  assert.equal(byId(exported, MUTED_ID).muted, true);
  assert.equal(byId(exported, 5).muted, false);
});

test("the CSV leaves muted IDs out, or includes them with a muted column", () => {
  const without = exportCsv(FIXTURE_CATALOGUE, copyFromDevice(), {}, withMutes(false)).split("\r\n");
  const withMuted = exportCsv(FIXTURE_CATALOGUE, copyFromDevice(), {}, withMutes(true)).split("\r\n");

  assert.equal(without.some((line) => line.startsWith(`${MUTED_ID},`)), false);
  assert.equal(withMuted[0].endsWith(",note,muted"), true);
  assert.equal(withMuted.find((line) => line.startsWith(`${MUTED_ID},`)).endsWith(",true"), true);
  assert.equal(withMuted.find((line) => line.startsWith("5,")).endsWith(",false"), true);
});

// ───── experiments ─────

const ESCAPED_RAW = "\u0001xÿ";
const PROBE_STEPS = [{ kind: "get", outcome: "ok", value: "01", at: 1000 }];
const PROBE_AT_MS = Date.UTC(2026, 10, 22, 9, 40, 0);
const probeEntry = (fields) => ({
  kind: "probe",
  id: 5,
  value: "02",
  original: "01",
  steps: PROBE_STEPS,
  verdict: "writable",
  restore: "failed",
  atMs: PROBE_AT_MS,
  ...fields,
});
/** A probe entry as the export writes it: `at` as wall ISO time instead of `atMs`. */
const probeJson = ({ atMs, ...probe }) => ({ ...probe, at: new Date(atMs).toISOString() });

/** 5 is a candidate (A, B, A), 40 a counter (disqualified), MUTED_ID answers once and NAKs once. */
function fanExperiment() {
  const captures = [
    ["A", { 5: "01", [MUTED_ID]: ESCAPED_RAW, 40: "1" }],
    ["B", { 5: "02", [MUTED_ID]: NAK, 40: "2" }],
    [" a", { 5: "01", 40: "3" }],
  ];
  return {
    ...experimentWith(captures, { range: { from: 1, to: 50 } }),
    probes: [probeEntry({}), probeEntry({ verdict: null, restore: null, interrupted: true })],
  };
}

const exportedExperiments = (options) =>
  JSON.parse(exportJson(FIXTURE_CATALOGUE, copyFromDevice(), {}, { ...NO_EXPERIMENTS, ...options }, BROWSER_NOW_MS))
    .experiments;

test("the JSON export carries each experiment with its range, captures, derived candidates and probes", () => {
  const experiment = fanExperiment();

  const [exported] = exportedExperiments({ experiments: [experiment] });

  assert.deepEqual(exported, {
    name: "fan 1 mode",
    from: 1,
    to: 50,
    created_at: new Date(experiment.createdAtMs).toISOString(),
    captures: experiment.captures.map((capture) => ({
      label: capture.label,
      at: new Date(capture.atMs).toISOString(),
      uptime_ms: capture.uptimeMs,
      boot: capture.boot,
      full: true,
      entries: capture.entries,
    })),
    candidates: [
      {
        id: 5,
        status: "candidate",
        values: [
          { label: "A", raw: "01", count: 2 },
          { label: "B", raw: "02", count: 1 },
        ],
      },
    ],
    disqualified: [{ id: 40, conflicts: [{ label: "A", raws: ["1", "3"] }] }],
    probes: experiment.probes.map(probeJson),
  });
});

test("an experiment with no capture yet exports a null range", () => {
  const [exported] = exportedExperiments({ experiments: [experimentWith([], { range: null })] });

  assert.equal(exported.from, null);
  assert.equal(exported.to, null);
  assert.deepEqual(exported.captures, []);
});

test("capture entries keep control and non-ASCII bytes byte-exact", () => {
  const [exported] = exportedExperiments({ experiments: [fanExperiment()] });

  assert.deepEqual([...exported.captures[0].entries[MUTED_ID].raw].map((c) => c.charCodeAt(0)), [0x01, 0x78, 0xff]);
});

test("muted IDs are left out of capture entries unless asked for, and never are candidates", () => {
  const muted = new Set([MUTED_ID, 5]);

  const [without] = exportedExperiments({ experiments: [fanExperiment()], mutes: muted });
  const [withMuted] = exportedExperiments({ experiments: [fanExperiment()], mutes: muted, includeMuted: true });

  assert.deepEqual(without.captures[0].entries, { 40: { outcome: "ok", raw: "1" } });
  assert.deepEqual(withMuted.captures[0].entries[5], { outcome: "ok", raw: "01", muted: true });
  assert.deepEqual(withMuted.captures[0].entries[40], { outcome: "ok", raw: "1", muted: false });
  for (const exported of [without, withMuted]) {
    assert.deepEqual(exported.candidates, []);
  }
  const mutedCounter = { experiments: [fanExperiment()], mutes: new Set([40]), includeMuted: true };
  assert.deepEqual(exportedExperiments(mutedCounter)[0].disqualified, []);
});

test("known IDs are never candidates", () => {
  const known = experimentWith([["A", { [LEVEL]: "1" }], ["B", { [LEVEL]: "2" }], ["A", { [LEVEL]: "1" }]]);

  assert.deepEqual(exportedExperiments({ experiments: [known] })[0].candidates, []);
});

test("only the open experiment narrows the experiments, not the records", () => {
  const second = { ...experimentWith([], { name: "fan 2 mode" }), key: "e2" };
  const options = { ...NO_EXPERIMENTS, experiments: [fanExperiment(), second], openExperimentKey: "e2" };
  const exported = (onlyOpen) =>
    JSON.parse(exportJson(FIXTURE_CATALOGUE, copyFromDevice(), {}, { ...options, onlyOpen }, BROWSER_NOW_MS));

  assert.deepEqual(exported(false).experiments.map((e) => e.name), ["fan 1 mode", "fan 2 mode"]);
  assert.deepEqual(exported(true).experiments.map((e) => e.name), ["fan 2 mode"]);
  assert.equal(exported(true).records.length, exported(false).records.length);
});

const pendingProbe = (fields) => ({
  experimentKey: "e1",
  kind: "probe",
  id: 5,
  value: "02",
  original: null,
  steps: [],
  atMs: PROBE_AT_MS,
  ...fields,
});

test("a probe still running at export time is exported last, as the device reports it, with active", () => {
  const running = {
    active: true,
    kind: "probe",
    id: 5,
    value: "02",
    original: "01",
    steps: PROBE_STEPS,
    verdict: null,
    restore: null,
  };
  const copy = withProbe(copyFromDevice(), running);
  const options = { ...NO_EXPERIMENTS, experiments: [fanExperiment()], pendingProbe: pendingProbe({}) };

  const [exported] = JSON.parse(exportJson(FIXTURE_CATALOGUE, copy, {}, options, BROWSER_NOW_MS)).experiments;

  assert.equal(exported.probes.length, 3);
  assert.deepEqual(exported.probes.at(-1), { ...running, at: new Date(PROBE_AT_MS).toISOString() });
});

test("a pending probe the device has not reported yet is exported as running with its request", () => {
  const restore = pendingProbe({ kind: "restore", value: "01", original: "01" });

  const [exported] = exportedExperiments({ experiments: [fanExperiment()], pendingProbe: restore });

  assert.deepEqual(exported.probes.at(-1), {
    active: true,
    kind: "restore",
    id: 5,
    value: "01",
    original: "01",
    steps: [],
    verdict: null,
    restore: null,
    at: new Date(PROBE_AT_MS).toISOString(),
  });
});

test("a pending probe of another experiment stays out of this one", () => {
  const other = pendingProbe({ experimentKey: "other" });

  assert.equal(exportedExperiments({ experiments: [fanExperiment()], pendingProbe: other })[0].probes.length, 2);
});

test("the JSON scan object carries ids for a list run", () => {
  const listScan = { ...SCAN, from: null, to: null, ids: [5, 40] };
  const copy = applyResponse(emptyCopy(), deviceResponse([], { scan: listScan }), BROWSER_NOW_MS).copy;

  assert.deepEqual(JSON.parse(exportJson(FIXTURE_CATALOGUE, copy, {}, NO_EXPERIMENTS, BROWSER_NOW_MS)).scan, listScan);
});
