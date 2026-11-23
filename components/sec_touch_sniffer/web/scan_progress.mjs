/**
 * The scan control's label and progress bar, from the `scan` object of /sniffer/data
 * (see .specs/sniffer.md, "Scan control" and "JSON shape").
 */

import { isKnownId, knownIds } from "./known_meaning.mjs";
import { ascending } from "./records.mjs";

const clampPercent = (percent) => Math.min(100, Math.max(0, Math.round(percent)));

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs: `goals` names the goals, its known IDs are never queried
 *   in discover
 * @param {{goal: string, from: number | null, to: number | null, ids: number[] | null}} scan the device's scan state
 * @returns {number[] | null} the IDs the run queries, ascending: check's known IDs, a list run's listed unknown IDs,
 *   or a discover range minus the known IDs; null before any run
 */
export function runIds(catalogue, scan) {
  if (scan.goal === catalogue.goals.check) {
    return knownIds(catalogue);
  }
  if (Array.isArray(scan.ids)) {
    return scan.ids.filter((id) => !isKnownId(catalogue, id)).sort(ascending);
  }
  if (scan.from === null || scan.to === null) {
    return null;
  }
  const ids = [];
  for (let id = scan.from; id <= scan.to; id++) {
    if (!isKnownId(catalogue, id)) {
      ids.push(id);
    }
  }
  return ids;
}

/** How many of `ids` are behind the scan: all of them once it is over. */
function behindCount(scan, ids) {
  return scan.done ? ids.length : ids.filter((id) => id < scan.next).length;
}

/** A discover range run: its progress through its range. */
function rangeProgress(scan) {
  if (scan.done) {
    return { label: `Scan of ${scan.from}–${scan.to} complete`, percent: 100 };
  }
  const percent = clampPercent((100 * (scan.next - scan.from)) / (scan.to - scan.from + 1));
  if (scan.active) {
    return { label: `Scanning ID ${scan.next} of ${scan.from}–${scan.to}`, percent };
  }
  return { label: `Stopped at ID ${scan.next}`, percent };
}

/** A run through a list of IDs (check's known IDs or a list run's): its progress through them. */
function listProgress(scan, ids, what, doneLabel) {
  if (scan.done) {
    return { label: doneLabel, percent: 100 };
  }
  const behind = behindCount(scan, ids);
  const percent = clampPercent((100 * behind) / ids.length);
  if (scan.active) {
    const position = Math.min(behind + 1, ids.length);
    return { label: `Scanning ID ${scan.next} (${position} of ${ids.length} ${what})`, percent };
  }
  return { label: `Stopped at ID ${scan.next}`, percent };
}

/**
 * @param {{active: boolean, goal: string, from: number | null, to: number | null, ids: number[] | null,
 *   next: number | null, done: boolean}} scan the device's scan state; `from`/`to` are null for check, a list run and
 *   before any run, `ids` is null but for a list run, `next` is null before any run and after a completed one
 * @param {object} catalogue the CATALOGUE of catalogue.mjs: `goals` names the goals, check counts against its known
 *   IDs
 * @param {string} shownGoal the open tab's goal: a scan of another goal is labelled with its goal
 * @returns {{label: string, percent: number}} the label and how much of the run is behind the scan (0–100)
 */
export function scanProgress(scan, catalogue, shownGoal) {
  if (scan.next === null && !scan.done) {
    return { label: "Not scanned yet", percent: 0 };
  }
  const progress = scanRunProgress(scan, catalogue);
  if (scan.goal === shownGoal) {
    return progress;
  }
  const goalName = `${scan.goal.charAt(0).toUpperCase()}${scan.goal.slice(1)}`;
  return { ...progress, label: `${goalName} scan: ${progress.label}` };
}

function scanRunProgress(scan, catalogue) {
  if (scan.goal === catalogue.goals.check) {
    const ids = runIds(catalogue, scan);
    return listProgress(scan, ids, "known IDs", `Check of ${ids.length} known IDs complete`);
  }
  if (Array.isArray(scan.ids)) {
    const ids = runIds(catalogue, scan);
    return listProgress(scan, ids, "listed IDs", `Scan of ${ids.length} listed IDs complete`);
  }
  return rangeProgress(scan);
}

/**
 * @param {number} count how many candidates
 * @returns {string} e.g. `1 candidate`, `3 candidates`
 */
export function candidateCount(count) {
  return `${count} ${count === 1 ? "candidate" : "candidates"}`;
}

/**
 * The scan row while this page's own capture runs.
 *
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {{label: string, experimentName: string, next: {full: boolean}}} capture the capture in flight (see
 *   capture_run.mjs): `next.full` tells a full range from narrowed candidates
 * @param {object} scan the device's scan state: the capture's run
 * @returns {{label: string, percent: number}} e.g. `capturing “boost” for fan 1 mode · 34/239 · full range`
 */
export function captureProgress(catalogue, capture, scan) {
  const ids = runIds(catalogue, scan) ?? [];
  const behind = scan.next === null && !scan.done ? 0 : behindCount(scan, ids);
  const scope = capture.next.full ? "full range" : candidateCount(ids.length);
  return {
    label: `capturing “${capture.label.trim()}” for ${capture.experimentName} · ${behind}/${ids.length} · ${scope}`,
    percent: ids.length === 0 ? 0 : clampPercent((100 * behind) / ids.length),
  };
}
