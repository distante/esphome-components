import { test } from "node:test";
import assert from "node:assert/strict";

import {
  analyseExperiment,
  coachStep,
  excludedFromExperiments,
  labelKey,
  labelsUsed,
  newExperiment,
  nextCapture,
  withCapture,
  withoutCapture,
} from "./experiments.mjs";
import { CANDIDATE_ID, capture, experimentWith, NAK, TIMEOUT, walkOf } from "./fixture_experiments.mjs";
import { FIXTURE_CATALOGUE } from "./fixture_catalogue.mjs";

const ID = CANDIDATE_ID;
const OTHER_ID = 90;
const none = () => false;

const analyse = (experiment, excluded = none) => analyseExperiment(experiment, excluded);
const candidateIds = (experiment) => analyse(experiment).candidates.map((candidate) => candidate.id);
const statusOf = (experiment, id = ID) => analyse(experiment).candidates.find((c) => c.id === id)?.status ?? null;
const disqualifiedIds = (experiment) => analyse(experiment).disqualified.map((entry) => entry.id);

// ───── labels ─────

test("labels are the same after trimming outer whitespace, ignoring case", () => {
  assert.equal(labelKey("  Boost "), labelKey("boost"));
  assert.notEqual(labelKey("boost 2"), labelKey("boost"));
});

test("the labels used are listed once each, in their first spelling", () => {
  const experiment = experimentWith([["Boost", {}], ["normal", {}], [" boost ", {}]]);

  assert.deepEqual(labelsUsed(experiment), ["Boost", "normal"]);
});

// ───── candidates (.specs/sniffer.md, "Experiments") ─────

test("a round trip X = v, Y = w, X = v makes a candidate", () => {
  const experiment = walkOf(ID, [["A", "01"], ["B", "02"], ["A", "01"]]);

  assert.deepEqual(analyse(experiment).candidates, [
    {
      id: ID,
      status: "candidate",
      values: [
        { label: "A", raw: "01", count: 2 },
        { label: "B", raw: "02", count: 1 },
      ],
    },
  ]);
});

test("A, B, C with 01, 02, 03 has no return and is not a candidate", () => {
  const experiment = walkOf(ID, [["A", "01"], ["B", "02"], ["C", "03"]]);

  assert.deepEqual(candidateIds(experiment), []);
  assert.deepEqual(disqualifiedIds(experiment), []);
});

test("an ID that changes for only some labels can be a candidate", () => {
  const experiment = walkOf(ID, [["normal", "01"], ["boost", "03"], ["normal", "01"], ["eco", "01"]]);

  assert.equal(statusOf(experiment), "candidate");
});

test("a round trip needs the values to differ byte for byte", () => {
  assert.deepEqual(candidateIds(walkOf(ID, [["A", "01"], ["B", "01"], ["A", "01"]])), []);
  assert.equal(statusOf(walkOf(ID, [["A", "01"], ["B", "01 "], ["A", "01"]])), "candidate");
});

test("a timeout is skipped in the walk", () => {
  const experiment = walkOf(ID, [["A", "01"], ["B", TIMEOUT], ["B", "02"], ["A", "01"]]);

  assert.equal(statusOf(experiment), "candidate");
});

test("a capture that did not query the ID is skipped in the walk", () => {
  const experiment = experimentWith([
    ["A", { [ID]: "01" }],
    ["B", { [OTHER_ID]: "05" }],
    ["B", { [ID]: "02" }],
    ["A", { [ID]: "01" }],
  ]);

  assert.equal(statusOf(experiment), "candidate");
});

test("a nak between two entries breaks the round trip", () => {
  const experiment = walkOf(ID, [["A", "01"], ["B", NAK], ["A", "01"]]);

  assert.deepEqual(candidateIds(experiment), []);
  assert.deepEqual(disqualifiedIds(experiment), []);
});

test("a nak inside the walk breaks the triple it sits in, not a later round trip", () => {
  const experiment = walkOf(ID, [["C", NAK], ["A", "01"], ["B", "02"], ["A", "01"]]);

  assert.equal(statusOf(experiment), "candidate");
  assert.deepEqual(analyse(experiment).candidates[0].values[0], { label: "C", raw: null, count: 1 });
});

test("a counter A=1, B=2, A=3 is disqualified at its third capture", () => {
  const twoCaptures = walkOf(ID, [["A", "1"], ["B", "2"]]);
  const threeCaptures = walkOf(ID, [["A", "1"], ["B", "2"], ["A", "3"]]);

  assert.deepEqual(disqualifiedIds(twoCaptures), []);
  assert.deepEqual(analyse(threeCaptures).disqualified, [{ id: ID, conflicts: [{ label: "A", raws: ["1", "3"] }] }]);
  assert.deepEqual(candidateIds(threeCaptures), []);
});

test("one disagreeing capture disqualifies a candidate", () => {
  const experiment = walkOf(ID, [["A", "01"], ["B", "02"], ["A", "01"], ["B", "07"]]);

  assert.deepEqual(candidateIds(experiment), []);
  assert.deepEqual(analyse(experiment).disqualified, [{ id: ID, conflicts: [{ label: "B", raws: ["02", "07"] }] }]);
});

test("a label that got a value once and a nak another time disqualifies the ID", () => {
  const experiment = walkOf(ID, [["A", "01"], ["B", "02"], ["A", NAK]]);

  assert.deepEqual(analyse(experiment).disqualified, [{ id: ID, conflicts: [{ label: "A", raws: ["01", null] }] }]);
});

test("the conflicts name the label in its first spelling", () => {
  const experiment = walkOf(ID, [["Boost", "1"], [" BOOST", "2"]]);

  assert.deepEqual(analyse(experiment).disqualified, [{ id: ID, conflicts: [{ label: "Boost", raws: ["1", "2"] }] }]);
});

test("A, B, A, B with agreeing values is corroborated", () => {
  assert.equal(statusOf(walkOf(ID, [["A", "01"], ["B", "02"], ["A", "01"], ["B", "02"]])), "corroborated");
});

test("labels that differ only in case and whitespace count as one label", () => {
  assert.equal(statusOf(walkOf(ID, [["Boost", "03"], ["normal", "01"], ["boost ", "03"], [" Normal", "01"]])),
    "corroborated");
});

test("corroboration counts only captures that gave a value, not naks", () => {
  const experiment = walkOf(ID, [["A", "01"], ["B", "02"], ["A", "01"], ["C", NAK], ["C", NAK]]);

  assert.equal(statusOf(experiment), "candidate");
});

test("an excluded (known or muted) ID is never a candidate nor disqualified", () => {
  const candidate = walkOf(ID, [["A", "01"], ["B", "02"], ["A", "01"], ["B", "02"]]);
  const counter = walkOf(ID, [["A", "1"], ["B", "2"], ["A", "3"]]);
  const excluded = (id) => id === ID;

  assert.deepEqual(analyse(candidate, excluded).candidates, []);
  assert.deepEqual(analyse(counter, excluded).disqualified, []);
});

test("known and muted IDs are left out of experiments, others are not", () => {
  const [knownId] = FIXTURE_CATALOGUE.levelIds;
  const excluded = excludedFromExperiments(FIXTURE_CATALOGUE, new Set([OTHER_ID]));

  assert.equal(excluded(knownId), true);
  assert.equal(excluded(OTHER_ID), true);
  assert.equal(excluded(ID), false);
});

test("corroborated candidates come first, then by ID", () => {
  const experiment = experimentWith([
    ["A", { 5: "1", 9: "1", 7: "1" }],
    ["B", { 5: "2", 9: "2", 7: "2" }],
    ["A", { 5: "1", 9: "1", 7: "1" }],
    ["B", { 9: "2" }],
  ]);

  assert.deepEqual(
    analyse(experiment).candidates.map((c) => [c.id, c.status]),
    [[9, "corroborated"], [5, "candidate"], [7, "candidate"]],
  );
});

test("deleting a capture can bring a disqualified ID back", () => {
  const experiment = walkOf(ID, [["A", "01"], ["B", "02"], ["A", "01"], ["B", "07"]]);

  assert.equal(statusOf(withoutCapture(experiment, 3)), "candidate");
});

// ───── captures ─────

test("a new experiment has a name, no range yet, no captures and no probes", () => {
  const nowMs = Date.UTC(2026, 10, 22, 8, 0, 0);

  const experiment = newExperiment("  fan 1 mode ", nowMs, []);

  assert.equal(experiment.name, "fan 1 mode");
  assert.equal(experiment.createdAtMs, nowMs);
  assert.equal(experiment.range, null);
  assert.deepEqual(experiment.captures, []);
  assert.deepEqual(experiment.probes, []);
});

test("each experiment gets its own key, even when created in the same millisecond", () => {
  const nowMs = Date.UTC(2026, 10, 22, 8, 0, 0);
  const first = newExperiment("a", nowMs, []);

  const second = newExperiment("b", nowMs, [first]);

  assert.notEqual(second.key, first.key);
});

test("the first capture fixes the experiment's full range; later ones keep it", () => {
  const range = { from: 17, to: 200 };
  const empty = experimentWith([], { range: null });

  const once = withCapture(empty, capture("A", { [ID]: "01" }), range);
  const twice = withCapture(once, capture("B", { [ID]: "02" }), { from: 1, to: 255 });

  assert.deepEqual(once.range, range);
  assert.deepEqual(twice.range, range);
  assert.deepEqual(twice.captures.map((c) => c.label), ["A", "B"]);
});

// ───── next capture ─────

const CURRENT_RANGE = { from: 3, to: 99 };
const next = (experiment, forceFull = false) =>
  nextCapture(FIXTURE_CATALOGUE, experiment, analyse(experiment), CURRENT_RANGE, forceFull);

test("an experiment with no capture yet captures the discover range in the input", () => {
  assert.deepEqual(next(experimentWith([], { range: null })), { full: true, ...CURRENT_RANGE });
});

test("without candidates the next capture is the experiment's full range, not the input's", () => {
  const range = { from: 17, to: 200 };
  const experiment = experimentWith([["A", { [ID]: "01" }]], { range });

  assert.deepEqual(next(experiment), { full: true, ...range });
});

test("with candidates the next capture queries only them, by ID", () => {
  const experiment = experimentWith([
    ["A", { 9: "1", 5: "1", 6: "1" }],
    ["B", { 9: "2", 5: "2", 6: "1" }],
    ["A", { 9: "1", 5: "1", 6: "1" }],
  ]);

  assert.deepEqual(next(experiment), { full: false, ids: [5, 9] });
});

test("a full rescan captures the full range despite candidates", () => {
  const experiment = walkOf(ID, [["A", "01"], ["B", "02"], ["A", "01"]]);

  assert.deepEqual(next(experiment, true), { full: true, from: 1, to: 255 });
});

test("when every candidate is disqualified the next capture is full again", () => {
  const experiment = walkOf(ID, [["A", "01"], ["B", "02"], ["A", "01"], ["B", "07"]]);

  assert.equal(next(experiment).full, true);
});

test("more candidates than a list run takes capture the full range", () => {
  const cap = FIXTURE_CATALOGUE.scanListCap;
  const roundTrip = (count) => {
    const ids = Array.from({ length: count }, (_, i) => 10 + i);
    const values = (raw) => Object.fromEntries(ids.map((id) => [id, raw]));
    return experimentWith([["A", values("1")], ["B", values("2")], ["A", values("1")]]);
  };

  assert.equal(next(roundTrip(cap)).full, false);
  assert.equal(next(roundTrip(cap + 1)).full, true);
});

// ───── coach ─────

const coach = (experiment) => coachStep(experiment, analyse(experiment));

test("the coach asks for a first capture, then a different value", () => {
  assert.deepEqual(coach(experimentWith([])), { step: "first" });
  assert.deepEqual(coach(walkOf(ID, [["normal", "01"]])), { step: "change" });
  assert.deepEqual(coach(walkOf(ID, [["normal", "01"], [" Normal", "01"]])), { step: "change" });
});

test("after two labels and no candidate the coach asks for a round trip to the first label", () => {
  assert.deepEqual(coach(walkOf(ID, [["normal", "01"], ["boost", "03"]])), { step: "round trip", label: "normal" });
});

test("with candidates none corroborated the coach asks to capture a label seen once again", () => {
  assert.deepEqual(coach(walkOf(ID, [["normal", "01"], ["boost", "03"], ["normal", "01"]])), {
    step: "corroborate",
    label: "boost",
  });
});

test("with a corroborated candidate the coach suggests a write probe", () => {
  const experiment = walkOf(ID, [["A", "01"], ["B", "02"], ["A", "01"], ["B", "02"]]);

  assert.deepEqual(coach(experiment), { step: "probe", ids: [ID] });
});

test("with every candidate disqualified the coach says the next capture is full, however few labels", () => {
  assert.deepEqual(coach(walkOf(ID, [["A", "01"], ["A", "02"]])), { step: "full again" });
});

test("with no candidate left after a round trip the coach says the next capture is full", () => {
  assert.deepEqual(coach(walkOf(ID, [["A", "01"], ["B", "02"], ["A", "01"], ["B", "07"]])), { step: "full again" });
  assert.deepEqual(coach(walkOf(ID, [["A", "01"], ["B", "02"], ["C", "03"]])), { step: "full again" });
});
