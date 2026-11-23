/**
 * Write probes and restores on a corroborated candidate's card (.specs/sniffer.md, "Experiments", Write probe on a
 * card, and "Write probe"). DOM-free: the page sends the requests, stores the results and renders the text.
 *
 * A probe entry is the device's `probe` object (`kind, id, value, original, steps, verdict, restore`) without `active`,
 * plus `atMs`, the browser time it was started, and, for a probe the device no longer knew, `interrupted: true`.
 *
 * A pending probe is `{experimentKey, kind, id, value, original, steps, atMs}`: the probe or restore this page asked
 * for, kept in browser storage until its result arrives, with the original and steps as last reported.
 */

import { shown } from "./control_bytes.mjs";
import { isObject, isStringOrNull, OUTCOMES } from "./records.mjs";

const PROBE_KINDS = ["probe", "restore"];
const STEP_KINDS = ["get", "set"];
const VERDICTS = ["writable", "not writable", "unclear", "nothing written"];
const RESTORES = ["ok", "failed", "skipped"];

/** How a verdict reads on the card and in the note. */
const VERDICT_TEXT = {
  writable: "✓ writable",
  "not writable": "✗ not writable",
  unclear: "unclear",
  "nothing written": "nothing written",
};

/** How a step's outcome reads. */
const OUTCOME_TEXT = { ok: "ACK", nak: "NAK", timeout: "timeout" };

/**
 * The device checks only a value's format; which values may be probed is the page's rule.
 *
 * @param {object} catalogue the CATALOGUE of catalogue.mjs: `probeValueCap` and `probeForbiddenBytes`
 * @param {string} raw a raw value, one code point per byte
 * @returns {boolean} whether a SET can carry it: at most the cap and none of the forbidden bytes; blank is fine
 */
export function isProbeValue(catalogue, raw) {
  if (raw.length > catalogue.probeValueCap) {
    return false;
  }
  return ![...raw].some((char) => catalogue.probeForbiddenBytes.includes(char.charCodeAt(0)));
}

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {object} experiment the open experiment
 * @param {number} id a corroborated candidate
 * @param {string | null} currentRaw its current raw value
 * @returns {{values: string[], chosen: string | null}} the raw values its captures observed that a SET can carry, in
 *   the order first observed, and the one to select: the first that is not the current value
 */
export function probeChoices(catalogue, experiment, id, currentRaw) {
  const values = [];
  for (const capture of experiment.captures) {
    const entry = capture.entries[id];
    if (entry?.outcome === "ok" && !values.includes(entry.raw) && isProbeValue(catalogue, entry.raw)) {
      values.push(entry.raw);
    }
  }
  return { values, chosen: values.find((value) => value !== currentRaw) ?? values[0] ?? null };
}

/**
 * The body must be the value's exact bytes: a string body would go out as UTF-8.
 *
 * @param {"probe" | "restore"} kind a write probe, or a restore to `value`
 * @param {number} id the property ID
 * @param {string} value the value, one code point per byte
 * @returns {{path: string, body: Uint8Array}} the request's path and body
 */
export function probeRequest(kind, id, value) {
  return {
    path: `/sniffer/${kind}?id=${id}`,
    body: Uint8Array.from(value, (char) => char.charCodeAt(0)),
  };
}

/**
 * @param {string} experimentKey the experiment whose card started it
 * @param {"probe" | "restore"} kind a write probe, or a restore to `value`
 * @param {number} id the property ID
 * @param {string} value the value, one code point per byte
 * @param {number} atMs browser time it was started (epoch ms)
 * @returns {object} the pending probe before the device reports it: no steps yet, and an original known only for a
 *   restore, where it is the value restored to (.specs/sniffer.md, "JSON shape")
 */
export function newPendingProbe(experimentKey, kind, id, value, atMs) {
  return { experimentKey, kind, id, value, original: kind === "restore" ? value : null, steps: [], atMs };
}

const isOneOfOrNull = (values) => (value) => value === null || values.includes(value);

const isStep = (step) =>
  isObject(step) &&
  STEP_KINDS.includes(step.kind) &&
  OUTCOMES.includes(step.outcome) &&
  isStringOrNull(step.value) &&
  Number.isFinite(step.at);

/** The fields a pending probe shares with the device's `probe` object: what was asked, and what came of it so far. */
const hasRequestFields = (value) =>
  isObject(value) &&
  PROBE_KINDS.includes(value.kind) &&
  Number.isInteger(value.id) &&
  typeof value.value === "string" &&
  isStringOrNull(value.original) &&
  Array.isArray(value.steps) &&
  value.steps.every(isStep);

const hasResultFields = (value) =>
  hasRequestFields(value) && isOneOfOrNull(VERDICTS)(value.verdict) && isOneOfOrNull(RESTORES)(value.restore);

/**
 * @param {unknown} value a parsed JSON value
 * @returns {boolean} whether it is a whole `probe` object as .specs/sniffer.md, "JSON shape" lists it
 */
const isProbeObject = (value) => hasResultFields(value) && typeof value.active === "boolean";

/**
 * @param {unknown} value a stored value
 * @returns {boolean} whether it is a whole probe entry
 */
export const isProbeEntry = (value) =>
  hasResultFields(value) && Number.isFinite(value.atMs) && (value.interrupted === undefined || value.interrupted === true);

/**
 * @param {unknown} value a stored value
 * @returns {boolean} whether it is a whole pending probe
 */
export const isPendingProbe = (value) =>
  hasRequestFields(value) && typeof value.experimentKey === "string" && Number.isFinite(value.atMs);

/**
 * @param {string} bodyText the body of a probe or restore the device answered with 200
 * @returns {object | null} the `probe` object it carries; null when the body is no whole probe object, which only
 *   means the page learns the probe from the next poll
 */
export function probeReply(bodyText) {
  try {
    const probe = JSON.parse(bodyText);
    if (isProbeObject(probe)) {
      return probe;
    }
  } catch {
    // Not JSON: no probe object.
  }
  return null;
}

const isSameRun = (pending, probe) =>
  probe !== null && probe.kind === pending.kind && probe.id === pending.id && probe.value === pending.value;

/**
 * The device reports only its last probe or restore, so a pending one it no longer reports was lost to a reboot.
 * Judged from the `probe` object, not from `boot`: Clear data draws a new boot but keeps the probe.
 *
 * @param {object | null} pending the pending probe, null when none
 * @param {object | null} probe the device's `probe` object
 * @param {boolean} fresh whether the poll was sent late enough to show the request: an older one may still show the
 *   probe before it, so only a running probe of the same ID, kind and value (the device runs one at a time) is
 *   taken from it
 * @returns {{kind: "wait"} | {kind: "running", pending: object} | {kind: "result", entry: object} | {kind:
 *   "interrupted", entry: object}} `running` carries the pending probe with the original and steps the device
 *   reports, `result` and `interrupted` the probe entry to store (the pending probe is then done)
 */
export function reconcileProbe(pending, probe, fresh) {
  if (pending === null) {
    return { kind: "wait" };
  }
  if (isSameRun(pending, probe) && probe.active) {
    return { kind: "running", pending: { ...pending, original: probe.original, steps: probe.steps } };
  }
  if (!fresh) {
    return { kind: "wait" };
  }
  if (isSameRun(pending, probe)) {
    const { active, ...result } = probe;
    return { kind: "result", entry: { ...result, atMs: pending.atMs } };
  }
  return {
    kind: "interrupted",
    entry: {
      kind: pending.kind,
      id: pending.id,
      value: pending.value,
      original: pending.original,
      steps: pending.steps,
      verdict: null,
      restore: null,
      interrupted: true,
      atMs: pending.atMs,
    },
  };
}

/**
 * @param {object} pending the pending probe
 * @param {object | null} probe the device's `probe` object
 * @returns {object} the pending probe as a running `probe` object: as the device reports it once it runs, else as
 *   it was asked for
 */
export function runningProbe(pending, probe) {
  if (isSameRun(pending, probe)) {
    return { ...probe, active: true };
  }
  const { kind, id, value, original, steps } = pending;
  return { active: true, kind, id, value, original, steps, verdict: null, restore: null };
}

function stepText(step) {
  if (step.kind === "set") {
    return `SET ${shown(step.value)} → ${OUTCOME_TEXT[step.outcome]}`;
  }
  return `GET → ${step.outcome === "ok" ? shown(step.value) : OUTCOME_TEXT[step.outcome]}`;
}

const orUnknown = (raw) => (raw === null ? "unknown" : shown(raw));

/** "now Y" of a failed restore: the last GET's answer, unknown when that GET failed. */
function lastGetValue(probe) {
  const lastGet = probe.steps.findLast((step) => step.kind === "get");
  return lastGet?.outcome === "ok" ? lastGet.value : null;
}

/**
 * @param {object} probe a probe entry, or the device's `probe` object while it runs
 * @returns {{title: string, steps: string[], verdict: string | null, restore: string | null}} what the card shows:
 *   `verdict` also says a probe or restore runs or was interrupted, `restore` is null when none was needed or decided
 */
export function probeTexts(probe) {
  const isRestore = probe.kind === "restore";
  const title = `${isRestore ? "restore" : "write probe"} ${shown(probe.value)}`;
  let verdict = probe.verdict === null ? null : VERDICT_TEXT[probe.verdict];
  if (probe.interrupted && isRestore) {
    verdict = `restore interrupted: ID ${probe.id} may not be back at ${shown(probe.value)}`;
  } else if (probe.interrupted) {
    verdict = `probe interrupted: ID ${probe.id} may still be ${shown(probe.value)}, original ${orUnknown(probe.original)}`;
  } else if (probe.active) {
    verdict = isRestore ? "restoring…" : "probing…";
  }
  let restore = null;
  if (probe.restore === "ok") {
    restore = "restore ok";
  } else if (probe.restore === "failed") {
    const now = orUnknown(lastGetValue(probe));
    restore = `restore failed: ID ${probe.id} is now ${now}, was ${orUnknown(probe.original)}`;
  }
  return { title, steps: probe.steps.map(stepText), verdict, restore };
}

/**
 * @param {object} probe a probe entry
 * @returns {string} the line appended to the ID's note
 */
export function probeNote(probe) {
  const texts = probeTexts(probe);
  const parts = [texts.verdict, ...texts.steps, texts.restore].filter((part) => part !== null);
  return `${texts.title}: ${parts.join(" · ")}`;
}

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {object | null} probe an ID's latest probe entry
 * @returns {string | null} the original to offer a restore to: after a failed restore or an interrupted probe, when
 *   the original is known and a SET can carry it
 */
export function restoreOffer(catalogue, probe) {
  if (probe === null || !(probe.restore === "failed" || probe.interrupted)) {
    return null;
  }
  if (probe.original === null || !isProbeValue(catalogue, probe.original)) {
    return null;
  }
  return probe.original;
}

/**
 * @param {object} experiment an experiment
 * @param {number} id a property ID
 * @returns {object | null} the last probe or restore entry of that ID, null when none
 */
export function latestProbe(experiment, id) {
  return experiment.probes.findLast((probe) => probe.id === id) ?? null;
}
