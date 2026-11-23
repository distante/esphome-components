import { test } from "node:test";
import assert from "node:assert/strict";

import {
  isProbeEntry,
  isProbeValue,
  isPendingProbe,
  latestProbe,
  newKeptSet,
  newPendingProbe,
  probeChoices,
  probeNote,
  probeReply,
  probeRequest,
  probeTexts,
  reconcileProbe,
  restoreOffer,
} from "./write_probe.mjs";
import {
  CANDIDATE_ID,
  deviceProbe,
  experimentWith,
  NAK,
  pendingProbe,
  PROBE_AT_MS,
  PROBE_ORIGINAL,
  PROBE_VALUE,
  probeStep,
  TIMEOUT,
  walkOf,
} from "./fixture_experiments.mjs";
import { FIXTURE_CATALOGUE } from "./fixture_catalogue.mjs";

const CATALOGUE = FIXTURE_CATALOGUE;
const ID = CANDIDATE_ID;
const CAP = CATALOGUE.probeValueCap;
const [NUL, FORBIDDEN, ...MORE_FORBIDDEN] = CATALOGUE.probeForbiddenBytes.map((byte) => String.fromCharCode(byte));
// ───── pending probes ─────

test("a pending probe starts with no steps; only a restore knows its original: the value it restores to", () => {
  const probe = newPendingProbe("e1", "probe", ID, PROBE_VALUE, PROBE_AT_MS);
  const restore = newPendingProbe("e1", "restore", ID, PROBE_ORIGINAL, PROBE_AT_MS);

  const request = { experimentKey: "e1", id: ID, steps: [], atMs: PROBE_AT_MS };
  assert.deepEqual(probe, { ...request, kind: "probe", value: PROBE_VALUE, original: null });
  assert.deepEqual(restore, { ...request, kind: "restore", value: PROBE_ORIGINAL, original: PROBE_ORIGINAL });
  assert.equal(isPendingProbe(probe) && isPendingProbe(restore), true);
});

test("a set and keep is a restore to the chosen value that remembers what the ID was", () => {
  const kept = newKeptSet("e1", ID, PROBE_VALUE, PROBE_ORIGINAL, PROBE_AT_MS);

  assert.deepEqual(kept, {
    experimentKey: "e1",
    kind: "restore",
    id: ID,
    value: PROBE_VALUE,
    original: PROBE_VALUE,
    steps: [],
    atMs: PROBE_AT_MS,
    was: PROBE_ORIGINAL,
  });
  assert.equal(isPendingProbe(kept), true);
  assert.equal(isPendingProbe(newKeptSet("e1", ID, PROBE_VALUE, null, PROBE_AT_MS)), true);
  assert.equal(isPendingProbe({ ...kept, was: 3 }), false);
});

test("a set and keep's result and interruption keep what the ID was", () => {
  const kept = newKeptSet("e1", ID, PROBE_VALUE, PROBE_ORIGINAL, PROBE_AT_MS);
  const device = deviceProbe({ kind: "restore", original: PROBE_VALUE, verdict: null, restore: "ok",
    steps: [probeStep("set", "ok", PROBE_VALUE, 100), probeStep("get", "ok", PROBE_VALUE, 200)] });

  const result = reconcileProbe(kept, device, true);
  assert.equal(result.entry.was, PROBE_ORIGINAL);
  assert.equal(isProbeEntry(result.entry), true);
  const interrupted = reconcileProbe(kept, null, true);
  assert.equal(interrupted.entry.was, PROBE_ORIGINAL);
  assert.equal(isProbeEntry(interrupted.entry), true);
  assert.equal("was" in reconcileProbe(pendingProbe({}), deviceProbe({}), true).entry, false);
});

// ───── values ─────

test("a probe value is at most the cap and holds no NUL, TAB, STX or ETX", () => {
  assert.equal(isProbeValue(CATALOGUE, "x".repeat(CAP)), true);
  assert.equal(isProbeValue(CATALOGUE, "x".repeat(CAP + 1)), false);
  for (const byte of [NUL, FORBIDDEN, ...MORE_FORBIDDEN]) {
    assert.equal(isProbeValue(CATALOGUE, `0${byte}`), false, JSON.stringify(byte));
  }
  assert.equal(isProbeValue(CATALOGUE, "0ÿ"), true);
});

test("an observed blank value can be probed", () => {
  assert.equal(isProbeValue(CATALOGUE, ""), true);
  assert.equal(isProbeValue(CATALOGUE, "  "), true);
});

test("the choices are the values observed for the ID in the experiment, defaulting to one not current", () => {
  const experiment = walkOf(ID, [["normal", "01"], ["boost", "03"], ["normal", "01"], ["eco", "02"]]);

  assert.deepEqual(probeChoices(CATALOGUE, experiment, ID, "01"), { values: ["01", "03", "02"], chosen: "03" });
  assert.deepEqual(probeChoices(CATALOGUE, experiment, ID, "03"), { values: ["01", "03", "02"], chosen: "01" });
});

test("the choices leave out naks, timeouts, other IDs and values a SET can't carry", () => {
  const experiment = experimentWith([
    ["A", { [ID]: "01", 90: "09" }],
    ["B", { [ID]: NAK }],
    ["C", { [ID]: TIMEOUT }],
    ["D", { [ID]: "x".repeat(CAP + 1) }],
    ["E", { [ID]: `0${FORBIDDEN}` }],
    ["F", { [ID]: "" }],
  ]);

  assert.deepEqual(probeChoices(CATALOGUE, experiment, ID, "01"), { values: ["01", ""], chosen: "" });
});

test("with only the current value observed it is still offered", () => {
  assert.deepEqual(probeChoices(CATALOGUE, walkOf(ID, [["A", "01"]]), ID, "01"), { values: ["01"], chosen: "01" });
  assert.deepEqual(probeChoices(CATALOGUE, walkOf(ID, [["A", NAK]]), ID, "01"), { values: [], chosen: null });
});

// ───── requests ─────

test("a probe or restore goes to its endpoint with the value's exact bytes as the body", () => {
  const probe = probeRequest("probe", ID, "0\u0001ÿ");
  const restore = probeRequest("restore", ID, "");

  assert.equal(probe.path, `/sniffer/probe?id=${ID}`);
  assert.deepEqual([...probe.body], [0x30, 0x01, 0xff]);
  assert.equal(restore.path, `/sniffer/restore?id=${ID}`);
  assert.deepEqual([...restore.body], []);
});

test("a 200 to a probe or restore gives the probe object it carries", () => {
  const reply = deviceProbe({ active: true, original: null, steps: [], verdict: null, restore: null });

  assert.deepEqual(probeReply(JSON.stringify(reply)), reply);
});

test("a reply that is no whole probe object gives none", () => {
  const whole = deviceProbe({});
  for (const body of ["", "{", "null", "{}", JSON.stringify({ ...whole, steps: "none" }),
    JSON.stringify({ ...whole, id: String(ID) }), JSON.stringify({ ...whole, kind: "poke" }),
    JSON.stringify({ ...whole, steps: [{ kind: "get" }] }), JSON.stringify({ ...whole, original: 1 })]) {
    assert.equal(probeReply(body), null, body);
  }
});

// ───── pending probe and interrupted detection ─────

test("a poll from before the request was sent says nothing about the pending probe", () => {
  assert.deepEqual(reconcileProbe(pendingProbe({}), null, false), { kind: "wait" });
  assert.deepEqual(reconcileProbe(null, deviceProbe({}), true), { kind: "wait" });
});

test("while the device runs the probe the pending entry keeps its original and steps", () => {
  const running = deviceProbe({
    active: true,
    steps: [probeStep("get", "ok", "01", 1000)],
    verdict: null,
    restore: null,
  });

  assert.deepEqual(reconcileProbe(pendingProbe({}), running, true), {
    kind: "running",
    pending: pendingProbe({ original: "01", steps: running.steps }),
  });
});

// The device runs one probe at a time: a running probe of this ID and value is this request, whenever the poll left.
test("a poll from before the request may already show it running, and keeps its original", () => {
  const running = deviceProbe({
    active: true,
    steps: [probeStep("get", "ok", "01", 1000)],
    verdict: null,
    restore: null,
  });

  assert.deepEqual(reconcileProbe(pendingProbe({}), running, false), {
    kind: "running",
    pending: pendingProbe({ original: "01", steps: running.steps }),
  });
  assert.deepEqual(reconcileProbe(pendingProbe({}), deviceProbe({}), false), { kind: "wait" });
});

test("a finished probe is the result, stamped with the time it was started", () => {
  const { active, ...result } = deviceProbe({});

  assert.equal(active, false);
  assert.deepEqual(reconcileProbe(pendingProbe({}), deviceProbe({}), true), {
    kind: "result",
    entry: { ...result, atMs: PROBE_AT_MS },
  });
});

test("a probe the device no longer reports was interrupted, with its original and steps as last seen", () => {
  const seen = [probeStep("get", "ok", PROBE_ORIGINAL, 1000), probeStep("set", "ok", PROBE_VALUE, 1400)];
  const pending = pendingProbe({ original: PROBE_ORIGINAL, steps: seen });

  for (const reported of [null, deviceProbe({ id: 78 }), deviceProbe({ value: "02" }), deviceProbe({ kind: "restore" })]) {
    assert.deepEqual(reconcileProbe(pending, reported, true), {
      kind: "interrupted",
      entry: {
        kind: "probe",
        id: ID,
        value: PROBE_VALUE,
        original: PROBE_ORIGINAL,
        steps: seen,
        verdict: null,
        restore: null,
        interrupted: true,
        atMs: PROBE_AT_MS,
      },
    }, JSON.stringify(reported));
  }
});

// ───── what the card and the note say ─────

test("a writable probe lists its steps, the verdict and the restore", () => {
  const probe = deviceProbe({
    steps: [
      probeStep("get", "ok", "01", 1000),
      probeStep("set", "timeout", "03", 3000),
      probeStep("set", "ok", "03", 8000),
      probeStep("get", "timeout", null, 10000),
      probeStep("get", "ok", "03", 11000),
      probeStep("set", "nak", "01", 11500),
      probeStep("set", "ok", "01", 12000),
      probeStep("get", "nak", null, 12500),
      probeStep("get", "ok", "01", 13000),
    ],
  });

  assert.deepEqual(probeTexts(probe), {
    title: "write probe 03",
    steps: [
      "GET → 01",
      "SET 03 → timeout",
      "SET 03 → ACK",
      "GET → timeout",
      "GET → 03",
      "SET 01 → NAK",
      "SET 01 → ACK",
      "GET → NAK",
      "GET → 01",
    ],
    verdict: "✓ writable",
    restore: "restore ok",
  });
});

test("each verdict reads as the spec names it", () => {
  assert.equal(probeTexts(deviceProbe({ verdict: "not writable", restore: "skipped" })).verdict, "✗ not writable");
  assert.equal(probeTexts(deviceProbe({ verdict: "unclear" })).verdict, "unclear");
  assert.equal(probeTexts(deviceProbe({ verdict: "nothing written", restore: "skipped" })).verdict, "nothing written");
  assert.equal(probeTexts(deviceProbe({ restore: "skipped" })).restore, null);
});

test("a running probe or restore says so", () => {
  assert.equal(probeTexts(deviceProbe({ active: true, verdict: null, restore: null })).verdict, "probing…");
  assert.equal(probeTexts(deviceProbe({ active: true, kind: "restore", verdict: null, restore: null })).verdict,
    "restoring…");
});

test("a failed restore says what the ID is now, from the last GET, and what it was", () => {
  const failed = deviceProbe({
    steps: [probeStep("get", "ok", "01", 1000), probeStep("set", "ok", "03", 1400), probeStep("get", "ok", "03", 1500),
      probeStep("set", "ok", "01", 8000), probeStep("get", "timeout", null, 8500), probeStep("get", "ok", "02", 9000),
      probeStep("set", "ok", "01", 9600)],
    restore: "failed",
  });

  assert.equal(probeTexts(failed).restore, `restore failed: ID ${ID} is now 02, was 01`);
});

test("a failed restore whose last GET failed says the value now is unknown", () => {
  const failed = deviceProbe({
    steps: [
      probeStep("get", "ok", "01", 1000),
      probeStep("get", "ok", "02", 9000),
      probeStep("get", "timeout", null, 9500),
    ],
    restore: "failed",
  });

  assert.equal(probeTexts(failed).restore, `restore failed: ID ${ID} is now unknown, was 01`);
});

test("a restore request's result reads as a restore", () => {
  const restore = deviceProbe({ kind: "restore", value: "01", verdict: null, restore: "ok",
    steps: [probeStep("set", "ok", "01", 100), probeStep("get", "ok", "01", 200)] });

  assert.deepEqual(probeTexts(restore), {
    title: "restore 01",
    steps: ["SET 01 → ACK", "GET → 01"],
    verdict: null,
    restore: "restore ok",
  });
});

test("an interrupted probe says what the ID may still be and its original", () => {
  const interrupted = { ...deviceProbe({ verdict: null, restore: null }), interrupted: true };

  assert.equal(
    probeTexts(interrupted).verdict,
    `probe interrupted: ID ${ID} may still be ${PROBE_VALUE}, original ${PROBE_ORIGINAL}`,
  );
  assert.equal(probeTexts({ ...interrupted, original: null }).verdict,
    `probe interrupted: ID ${ID} may still be ${PROBE_VALUE}, original unknown`);
});

test("an interrupted restore says what the ID may not be back at", () => {
  const restore = deviceProbe({ kind: "restore", value: PROBE_ORIGINAL, verdict: null, restore: null });
  const interrupted = { ...restore, interrupted: true };

  assert.equal(probeTexts(interrupted).verdict, `restore interrupted: ID ${ID} may not be back at ${PROBE_ORIGINAL}`);
});

test("the note line holds the title, verdict, steps and restore", () => {
  assert.equal(
    probeNote(deviceProbe({})),
    "write probe 03: ✓ writable · GET → 01 · SET 03 → ACK · GET → 03 · restore ok",
  );
});

test("values show trimmed with control bytes escaped", () => {
  const probe = deviceProbe({ value: " 0\u0001 ", steps: [probeStep("set", "ok", " 0\u0001 ", 1)] });

  assert.equal(probeTexts(probe).title, "write probe 0\\x01");
  assert.deepEqual(probeTexts(probe).steps, ["SET 0\\x01 → ACK"]);
});

/** A set and keep of ID to PROBE_VALUE that took, from PROBE_ORIGINAL, as stored; `fields` overrides any field. */
const keptEntry = (fields) => ({
  ...deviceProbe({ kind: "restore", original: PROBE_VALUE, verdict: null, restore: "ok",
    steps: [probeStep("set", "ok", PROBE_VALUE, 100), probeStep("get", "ok", PROBE_VALUE, 200)] }),
  atMs: PROBE_AT_MS,
  was: PROBE_ORIGINAL,
  ...fields,
});

test("a set and keep that took says the ID is kept at the value, and what it was", () => {
  assert.deepEqual(probeTexts(keptEntry({})), {
    title: `set and keep ${PROBE_VALUE}`,
    steps: [`SET ${PROBE_VALUE} → ACK`, `GET → ${PROBE_VALUE}`],
    verdict: null,
    restore: `kept: ID ${ID} is ${PROBE_VALUE}, was ${PROBE_ORIGINAL}`,
  });
  assert.equal(probeTexts(keptEntry({ was: null })).restore, `kept: ID ${ID} is ${PROBE_VALUE}, was unknown`);
  assert.equal(
    probeNote(keptEntry({})),
    `set and keep ${PROBE_VALUE}: SET ${PROBE_VALUE} → ACK · GET → ${PROBE_VALUE} · kept: ID ${ID} is ${PROBE_VALUE}, was ${PROBE_ORIGINAL}`,
  );
});

test("a set and keep that failed, runs or was interrupted says so, with what the ID was", () => {
  const failed = keptEntry({ restore: "failed", steps: [probeStep("set", "nak", PROBE_VALUE, 100),
    probeStep("get", "ok", "02", 200)] });

  assert.equal(probeTexts(failed).restore, `set failed: ID ${ID} is now 02, was ${PROBE_ORIGINAL}`);
  assert.equal(probeTexts(keptEntry({ active: true, restore: null })).verdict, "setting…");
  assert.equal(probeTexts(keptEntry({ restore: null, interrupted: true })).verdict,
    `set interrupted: ID ${ID} may be ${PROBE_VALUE}, was ${PROBE_ORIGINAL}`);
});

// ───── restore button ─────

test("a failed restore or an interrupted probe offers to restore the original", () => {
  assert.equal(restoreOffer(CATALOGUE, deviceProbe({ restore: "failed" })), PROBE_ORIGINAL);
  const interrupted = { ...deviceProbe({ verdict: null, restore: null }), interrupted: true };
  assert.equal(restoreOffer(CATALOGUE, interrupted), PROBE_ORIGINAL);
});

test("after a set and keep, whatever came of it, the offer is to set the ID back to what it was", () => {
  for (const fields of [{}, { restore: "failed" }, { restore: null, interrupted: true }]) {
    assert.equal(restoreOffer(CATALOGUE, keptEntry(fields)), PROBE_ORIGINAL, JSON.stringify(fields));
  }
  assert.equal(restoreOffer(CATALOGUE, keptEntry({ was: null })), null);
  assert.equal(restoreOffer(CATALOGUE, keptEntry({ was: "x".repeat(CAP + 1) })), null);
});

test("there is nothing to restore without a failure, without an original, or to an original a SET can't carry", () => {
  assert.equal(restoreOffer(CATALOGUE, deviceProbe({})), null);
  assert.equal(restoreOffer(CATALOGUE, { ...deviceProbe({ original: null, restore: null }), interrupted: true }), null);
  assert.equal(restoreOffer(CATALOGUE, deviceProbe({ restore: "failed", original: "x".repeat(CAP + 1) })), null);
  assert.equal(restoreOffer(CATALOGUE, null), null);
});

test("the latest probe of an ID is its last entry", () => {
  const experiment = {
    ...walkOf(ID, []),
    probes: [deviceProbe({ verdict: "unclear" }), deviceProbe({ id: 78 }), deviceProbe({ verdict: "writable" })],
  };

  assert.equal(latestProbe(experiment, ID).verdict, "writable");
  assert.equal(latestProbe(experiment, 79), null);
});
