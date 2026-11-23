/**
 * Test fixture only, never imported by the page: experiments and captures as experiments.mjs stores them
 * (.specs/sniffer.md, "Experiments").
 */

import { FIXTURE_BOOT } from "./fixture_records.mjs";
import { newPendingProbe } from "./write_probe.mjs";

/** An unknown property ID the fixture experiments walk and probe. */
export const CANDIDATE_ID = 77;

/** An entry of a capture whose query got NAK. */
export const NAK = { outcome: "nak", raw: null };

/** An entry of a capture whose query timed out. */
export const TIMEOUT = { outcome: "timeout", raw: null };

/** The browser time of the first fixture capture; each further one is a minute later. */
const FIRST_CAPTURE_MS = Date.UTC(2026, 10, 22, 9, 0, 0);
const MINUTE_MS = 60_000;

/**
 * A capture labelled `label` with one entry per key of `values`: a string is an ok answer with that raw value,
 * NAK and TIMEOUT are those outcomes.
 */
export function capture(label, values, { index = 0, full = true, boot = FIXTURE_BOOT } = {}) {
  const entries = Object.fromEntries(
    Object.entries(values).map(([id, value]) => [id, typeof value === "string" ? { outcome: "ok", raw: value } : value]),
  );
  return { label, atMs: FIRST_CAPTURE_MS + index * MINUTE_MS, uptimeMs: 5000 + index * MINUTE_MS, boot, full, entries };
}

/**
 * An experiment holding one capture per `[label, values]` pair, in that order (see `capture`).
 */
export function experimentWith(pairs, { name = "fan 1 mode", range = { from: 1, to: 255 } } = {}) {
  return {
    key: "e1",
    name,
    createdAtMs: FIRST_CAPTURE_MS - MINUTE_MS,
    range,
    captures: pairs.map(([label, values], index) => capture(label, values, { index })),
    probes: [],
  };
}

/** One ID's walk: an experiment whose captures give ID `id` the `[label, value]` pairs in order. */
export function walkOf(id, pairs) {
  return experimentWith(pairs.map(([label, value]) => [label, { [id]: value }]));
}

/**
 * This page's capture in flight as capture_run.mjs documents it: scanning `next` (experiments.mjs, `nextCapture`)
 * for the experiment "fan 1 mode", labelled " boost " as typed. `fields` overrides any of its fields.
 */
export function captureInFlight(next, fields = {}) {
  return { experimentKey: "e1", experimentName: "fan 1 mode", label: " boost ", next, boot: FIXTURE_BOOT, ...fields };
}

/** The browser time the fixture probes were started. */
export const PROBE_AT_MS = Date.UTC(2026, 10, 22, 9, 40, 0);
/** The value the fixture probe writes, and the value its ID held before. */
export const PROBE_VALUE = "03";
export const PROBE_ORIGINAL = "01";

/** A step of a probe or restore as the device reports it. */
export const probeStep = (kind, outcome, value, at) => ({ kind, outcome, value, at });

/**
 * The device's `probe` object of a finished, writable probe of CANDIDATE_ID from PROBE_ORIGINAL to PROBE_VALUE,
 * restored; `fields` overrides any of its fields.
 */
export const deviceProbe = (fields) => ({
  active: false,
  kind: "probe",
  id: CANDIDATE_ID,
  value: PROBE_VALUE,
  original: PROBE_ORIGINAL,
  steps: [
    probeStep("get", "ok", PROBE_ORIGINAL, 1000),
    probeStep("set", "ok", PROBE_VALUE, 1400),
    probeStep("get", "ok", PROBE_VALUE, 1500),
  ],
  verdict: "writable",
  restore: "ok",
  ...fields,
});

/** `deviceProbe` as an experiment stores it (write_probe.mjs): without `active`, started at PROBE_AT_MS. */
export function probeEntry(fields) {
  const { active, ...entry } = deviceProbe({});
  return { ...entry, atMs: PROBE_AT_MS, ...fields };
}

/**
 * A pending probe of experiment "e1" as the page starts it (write_probe.mjs, `newPendingProbe`): by default a probe of
 * CANDIDATE_ID to PROBE_VALUE at PROBE_AT_MS. The request fields given start it, so a restore carries its original;
 * `original` and `steps`, when given, are what the device reported since.
 */
export function pendingProbe({
  experimentKey = "e1",
  kind = "probe",
  id = CANDIDATE_ID,
  value = PROBE_VALUE,
  atMs = PROBE_AT_MS,
  ...reported
}) {
  return { ...newPendingProbe(experimentKey, kind, id, value, atMs), ...reported };
}
