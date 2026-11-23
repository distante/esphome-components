/**
 * Experiments (.specs/sniffer.md, "Experiments"): captures of the panel at named values of one setting, and the
 * candidates derived from them. DOM-free; the page renders them and keeps them in browser storage.
 *
 * An experiment is `{key, name, createdAtMs, range, captures, probes}`: `range` is the full range `{from, to}` fixed at
 * its first capture (null before), `captures` are in the order they were stored, `probes` are the write probe and
 * restore results run from its cards (see write_probe.mjs), oldest first.
 *
 * A capture is `{label, atMs, uptimeMs, boot, full, entries}`: `label` as typed, `atMs` browser time, `uptimeMs` and
 * `boot` the device's then, `full` whether it scanned the full range, and `entries` maps each ID it queried to
 * `{outcome: "ok"|"nak"|"timeout", raw}` (raw null unless ok).
 */

import { knownIds } from "./known_meaning.mjs";
import { ascending } from "./records.mjs";

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {Set<number>} mutes the muted IDs
 * @returns {(id: number) => boolean} whether an ID is left out of experiments: a known or a muted one
 */
export function excludedFromExperiments(catalogue, mutes) {
  const known = new Set(knownIds(catalogue));
  return (id) => known.has(id) || mutes.has(id);
}

/**
 * @param {string} label a label as typed
 * @returns {string} what makes two labels the same: trimmed, ignoring case
 */
export function labelKey(label) {
  return label.trim().toLowerCase();
}

/**
 * @param {object} experiment an experiment
 * @returns {Map<string, string>} the first spelling (trimmed) of each label, by label key, in the order first used
 */
function firstSpellings(experiment) {
  const spellings = new Map();
  for (const capture of experiment.captures) {
    const key = labelKey(capture.label);
    if (!spellings.has(key)) {
      spellings.set(key, capture.label.trim());
    }
  }
  return spellings;
}

/**
 * @param {object} experiment an experiment
 * @returns {string[]} each label used, once, in its first spelling, in the order first used
 */
export function labelsUsed(experiment) {
  return [...firstSpellings(experiment).values()];
}

/**
 * @param {string} name the setting the experiment changes, as typed
 * @param {number} nowMs browser time (epoch ms)
 * @param {object[]} experiments the experiments already stored, so the key is new
 * @returns {object} a new experiment with no capture
 */
export function newExperiment(name, nowMs, experiments) {
  const taken = new Set(experiments.map((experiment) => experiment.key));
  let key = String(nowMs);
  for (let n = 2; taken.has(key); n++) {
    key = `${nowMs}-${n}`;
  }
  return { key, name: name.trim(), createdAtMs: nowMs, range: null, captures: [], probes: [] };
}

/**
 * @param {object} experiment an experiment, left unchanged
 * @param {object} capture the capture to store, last
 * @param {{from: number, to: number} | null} range the full range the capture ran with, null for a narrowed one; it
 *   becomes the experiment's full range when this is its first capture
 * @returns {object} the experiment with the capture
 */
export function withCapture(experiment, capture, range) {
  return {
    ...experiment,
    range: experiment.range ?? range,
    captures: [...experiment.captures, capture],
  };
}

/**
 * @param {object} experiment an experiment, left unchanged
 * @param {number} index the index of the capture to delete, in stored order
 * @returns {object} the experiment without that capture
 */
export function withoutCapture(experiment, index) {
  return { ...experiment, captures: experiment.captures.filter((_, i) => i !== index) };
}

/**
 * An ID's walk: its entries in capture order, skipping captures that timed out on it or did not query it.
 *
 * @returns {{key: string, outcome: string, raw: string | null}[]}
 */
function walkOf(experiment, id) {
  const walk = [];
  for (const capture of experiment.captures) {
    const entry = capture.entries[id];
    if (entry === undefined || entry.outcome === "timeout") {
      continue;
    }
    walk.push({ key: labelKey(capture.label), outcome: entry.outcome, raw: entry.outcome === "ok" ? entry.raw : null });
  }
  return walk;
}

/** Whether the walk holds X = v, Y = w, X = v with Y another label, w ≠ v, and no nak among the three. */
function hasRoundTrip(walk) {
  for (let i = 0; i + 2 < walk.length; i++) {
    const [x, y, z] = walk.slice(i, i + 3);
    if ([x, y, z].some((entry) => entry.outcome !== "ok")) {
      continue;
    }
    if (x.key === z.key && y.key !== x.key && x.raw === z.raw && y.raw !== x.raw) {
      return true;
    }
  }
  return false;
}

/**
 * @param {object} experiment an experiment
 * @param {(id: number) => boolean} excluded whether an ID is left out of experiments: a known or muted one
 * @returns {{candidates: {id: number, status: "candidate"|"corroborated", values: {label: string, raw: string | null,
 *   count: number}[]}[], disqualified: {id: number, conflicts: {label: string, raws: (string | null)[]}[]}[]}} the
 *   candidates, corroborated first then by ID, and the disqualified IDs by ID; `values` lists each label the ID
 *   answered (raw null for a nak) with how often, `conflicts` each label whose captures disagree with their raws
 */
export function analyseExperiment(experiment, excluded) {
  const spellings = firstSpellings(experiment);
  const ids = new Set();
  for (const capture of experiment.captures) {
    for (const id of Object.keys(capture.entries)) {
      ids.add(Number(id));
    }
  }
  const candidates = [];
  const disqualified = [];
  for (const id of [...ids].sort(ascending)) {
    if (excluded(id)) {
      continue;
    }
    const walk = walkOf(experiment, id);
    // Per label: every raw seen (null for a nak), in walk order.
    const byLabel = new Map();
    for (const entry of walk) {
      byLabel.set(entry.key, [...(byLabel.get(entry.key) ?? []), entry]);
    }
    const conflicts = [...byLabel]
      .map(([key, entries]) => ({ label: spellings.get(key), raws: [...new Set(entries.map((entry) => entry.raw))] }))
      .filter((conflict) => conflict.raws.length > 1);
    if (conflicts.length > 0) {
      disqualified.push({ id, conflicts });
      continue;
    }
    if (!hasRoundTrip(walk)) {
      continue;
    }
    const repeatedLabels = [...byLabel.values()].filter(
      (entries) => entries.filter((entry) => entry.outcome === "ok").length >= 2,
    ).length;
    candidates.push({
      id,
      status: repeatedLabels >= 2 ? "corroborated" : "candidate",
      values: [...byLabel].map(([key, entries]) => ({
        label: spellings.get(key),
        raw: entries[0].raw,
        count: entries.length,
      })),
    });
  }
  const corroboratedFirst = (a, b) => (b.status === "corroborated") - (a.status === "corroborated") || a.id - b.id;
  return { candidates: candidates.sort(corroboratedFirst), disqualified };
}

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs: `scanListCap` is the most IDs a narrowed capture takes
 * @param {object} experiment the open experiment
 * @param {object} analysis its `analyseExperiment`
 * @param {{from: number, to: number}} currentRange the discover range in the input: the full range of a first capture
 * @param {boolean} forceFull whether "full rescan" was pressed
 * @returns {{full: true, from: number, to: number} | {full: false, ids: number[]}} what the next capture scans: the
 *   full range while there is no candidate, when forced, or with more candidates than a narrowed capture takes;
 *   else only the candidates, by ID
 */
export function nextCapture(catalogue, experiment, analysis, currentRange, forceFull) {
  const ids = analysis.candidates.map((candidate) => candidate.id).sort(ascending);
  if (forceFull || ids.length === 0 || ids.length > catalogue.scanListCap) {
    const range = experiment.range ?? currentRange;
    return { full: true, from: range.from, to: range.to };
  }
  return { full: false, ids };
}

/**
 * The coach's next-step prompt, steering toward a round trip and then corroboration.
 *
 * @param {object} experiment the open experiment
 * @param {object} analysis its `analyseExperiment`
 * @returns {{step: "first"} | {step: "change"} | {step: "round trip", label: string} | {step: "corroborate",
 *   label: string} | {step: "probe", ids: number[]} | {step: "full again"}} `round trip` names the label to go back
 *   to, `corroborate` a label to capture again, `probe` the corroborated IDs; `full again` when no candidate is left
 *   and an ID was disqualified, or after a round trip found none
 */
export function coachStep(experiment, analysis) {
  if (experiment.captures.length === 0) {
    return { step: "first" };
  }
  const { candidates } = analysis;
  const corroborated = candidates.filter((candidate) => candidate.status === "corroborated");
  if (corroborated.length > 0) {
    return { step: "probe", ids: corroborated.map((candidate) => candidate.id) };
  }
  if (candidates.length > 0) {
    const once = candidates[0].values.find((value) => value.count < 2) ?? candidates[0].values[0];
    return { step: "corroborate", label: once.label };
  }
  if (analysis.disqualified.length > 0) {
    return { step: "full again" };
  }
  const labels = labelsUsed(experiment);
  if (labels.length < 2) {
    return { step: "change" };
  }
  if (experiment.captures.length === 2) {
    return { step: "round trip", label: labels[0] };
  }
  return { step: "full again" };
}
