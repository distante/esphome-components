/**
 * The scan control's label and progress bar, from the `scan` object of /sniffer/data
 * (see .specs/sniffer.md, "Scan control" and "JSON shape").
 */

import { knownIds } from "./known_meaning.mjs";

const clampPercent = (percent) => Math.min(100, Math.max(0, Math.round(percent)));

/** A discover run: its progress through its range. */
function discoverProgress(scan) {
  if (scan.done) {
    return { label: `Scan of ${scan.from}–${scan.to} complete`, percent: 100 };
  }
  const percent = clampPercent((100 * (scan.next - scan.from)) / (scan.to - scan.from + 1));
  if (scan.active) {
    return { label: `Scanning ID ${scan.next} of ${scan.from}–${scan.to}`, percent };
  }
  return { label: `Stopped at ID ${scan.next}`, percent };
}

/** A check run: no range, its progress through the known IDs. */
function checkProgress(scan, catalogue) {
  const ids = knownIds(catalogue);
  if (scan.done) {
    return { label: `Check of ${ids.length} known IDs complete`, percent: 100 };
  }
  // The known IDs before the next one are behind the scan.
  const behind = ids.filter((id) => id < scan.next).length;
  const percent = clampPercent((100 * behind) / ids.length);
  if (scan.active) {
    const position = Math.min(behind + 1, ids.length);
    return { label: `Scanning ID ${scan.next} (${position} of ${ids.length} known IDs)`, percent };
  }
  return { label: `Stopped at ID ${scan.next}`, percent };
}

/**
 * @param {{active: boolean, goal: string, from: number | null, to: number | null, next: number | null,
 *   done: boolean}} scan the device's scan state; `from`/`to` are null for check and before any run, `next` is null
 *   before any run and after a completed one
 * @param {object} catalogue the CATALOGUE of catalogue.mjs: `goals` names the goals, check counts against its known
 *   IDs
 * @param {string} shownGoal the open tab's goal: a scan of another goal is labelled with its goal
 * @returns {{label: string, percent: number}} the label and how much of the run is behind the scan (0–100)
 */
export function scanProgress(scan, catalogue, shownGoal) {
  if (scan.next === null && !scan.done) {
    return { label: "Not scanned yet", percent: 0 };
  }
  const progress = scan.goal === catalogue.goals.check ? checkProgress(scan, catalogue) : discoverProgress(scan);
  if (scan.goal === shownGoal) {
    return progress;
  }
  const goalName = `${scan.goal.charAt(0).toUpperCase()}${scan.goal.slice(1)}`;
  return { ...progress, label: `${goalName} scan: ${progress.label}` };
}
