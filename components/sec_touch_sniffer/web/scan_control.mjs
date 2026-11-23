/**
 * The scan control's requests and button (see .specs/sniffer.md, "Goal switch", "Scan control" and the start
 * parameters of "Device/page contract"). DOM-free: the page sends the requests and renders the text.
 */

import { isObject, wholeDecimal } from "./records.mjs";

/** Shown for a refused start whose body carries no text. */
export const RANGE_REFUSED = "the device refused the range";

/** Shown for a refused probe or restore whose body carries no text. */
export const PROBE_REFUSED = "the device refused the value";

/** What runs, for a start refused with 409 whose reply names nothing running. */
export const BUSY_UNKNOWN = "another run";

/**
 * The device decides whether a range is valid, so the range goes out as typed.
 *
 * @param {object} catalogue the CATALOGUE of catalogue.mjs: `goals` names the goals as the device does
 * @param {string} goal one of `catalogue.goals`
 * @param {string} fromText the discover range's from, as typed
 * @param {string} toText the discover range's to, as typed
 * @returns {string} the start request's path and query; check sends no range
 */
export function scanStartPath(catalogue, goal, fromText, toText) {
  const path = `/sniffer/scan/start?goal=${encodeURIComponent(goal)}`;
  if (goal !== catalogue.goals.discover) {
    return path;
  }
  return `${path}&from=${encodeURIComponent(fromText)}&to=${encodeURIComponent(toText)}`;
}

/**
 * A capture is a discover scan of the experiment's full range, or of only its candidates as an ID list.
 *
 * @param {object} catalogue the CATALOGUE of catalogue.mjs: `goals` names the goals as the device does
 * @param {{full: true, from: number, to: number} | {full: false, ids: number[]}} next what the capture scans (see
 *   experiments.mjs, `nextCapture`)
 * @returns {string} the start request's path and query
 */
export function captureStartPath(catalogue, next) {
  const path = `/sniffer/scan/start?goal=${encodeURIComponent(catalogue.goals.discover)}`;
  return next.full ? `${path}&from=${next.from}&to=${next.to}` : `${path}&ids=${next.ids.join(",")}`;
}

/**
 * @param {{active: boolean} | null} scan the device's scan state, null before it answered
 * @param {{active: boolean} | null} probe the device's probe state, null before any probe or restore
 * @returns {boolean} whether the device runs a scan, a capture, a probe or a restore: no other start is taken then
 */
export function isRunning(scan, probe) {
  return Boolean(scan?.active || probe?.active);
}

/**
 * @param {{active: boolean, goal: string} | null} scan the device's scan state, null before it answered
 * @param {{active: boolean} | null} probe the device's probe state, null before any probe or restore
 * @param {string} goal the open tab's goal
 * @returns {{label: string, disabled: boolean}} the start/stop button: a stop names the running scan's goal and
 *   stops a scan or a capture; a start names the open tab's goal and is disabled while a probe or restore runs,
 *   which can't be stopped
 */
export function scanButton(scan, probe, goal) {
  if (scan?.active) {
    return { label: `■ Stop ${scan.goal} scan`, disabled: false };
  }
  return { label: `▶ Start ${goal} scan`, disabled: Boolean(probe?.active) };
}

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs: `goals` names the goals as the device does
 * @param {object | null} scan the device's scan object
 * @param {object | null} probe the device's probe object
 * @returns {string | null} what the device runs, for "busy: <what runs>"; null while it runs nothing
 */
export function runningText(catalogue, scan, probe) {
  if (probe?.active) {
    return `${probe.kind === "restore" ? "restore" : "write probe"} of ID ${probe.id}`;
  }
  if (!scan?.active) {
    return null;
  }
  if (scan.goal === catalogue.goals.discover && Array.isArray(scan.ids)) {
    return `${scan.goal} scan of ${scan.ids.length} listed IDs`;
  }
  return `${scan.goal} scan`;
}

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {string} bodyText the body of a start the device refused with 409: `{error, scan, probe}`
 * @returns {string} "busy: <what runs>", from the reply's scan and probe
 */
export function busyText(catalogue, bodyText) {
  let running = null;
  try {
    const body = JSON.parse(bodyText);
    if (isObject(body)) {
      running = runningText(catalogue, isObject(body.scan) ? body.scan : null, isObject(body.probe) ? body.probe : null);
    }
  } catch {
    // Not JSON: the device still said busy.
  }
  return `busy: ${running ?? BUSY_UNKNOWN}`;
}

/**
 * The page never changes a running scan's goal: switching to another goal's tab stops the scan, switching to the
 * running goal's tab just switches.
 *
 * @param {{active: boolean, goal: string} | null} scan the device's scan state, null before it answered
 * @param {string} targetGoal the goal of the tab switched to
 * @returns {boolean} whether the switch must stop the running scan (and so ask first)
 */
export function switchStopsScan(scan, targetGoal) {
  return Boolean(scan?.active) && scan.goal !== targetGoal;
}

/**
 * @param {string} bodyText the body of a request the device answered with 400
 * @param {string} fallback the text for a body with no text
 * @returns {string} the device's `error`, else the body, else the fallback
 */
function refusalText(bodyText, fallback) {
  try {
    const body = JSON.parse(bodyText);
    if (typeof body?.error === "string") {
      return body.error;
    }
  } catch {
    // Not JSON: the body itself is the best text there is.
  }
  return bodyText.trim() || fallback;
}

/**
 * @param {string} bodyText the body of a start the device answered with 400
 * @returns {string} the text to show next to the range: the device's `error`, else the body, else RANGE_REFUSED
 */
export function rangeError(bodyText) {
  return refusalText(bodyText, RANGE_REFUSED);
}

/**
 * @param {string} bodyText the body of a probe or restore the device answered with 400
 * @returns {string} the text to show on the card: the device's `error`, else the body, else PROBE_REFUSED
 */
export function probeError(bodyText) {
  return refusalText(bodyText, PROBE_REFUSED);
}

const isIdOrNull = (value) => value === null || Number.isInteger(value);

const isIdListOrNull = (value) => value === null || (Array.isArray(value) && value.every(Number.isInteger));

/**
 * @param {unknown} value a parsed JSON value
 * @returns {boolean} whether it is a whole `scan` object as .specs/sniffer.md, "JSON shape" lists it
 */
const isScanObject = (value) =>
  isObject(value) &&
  typeof value.active === "boolean" &&
  typeof value.goal === "string" &&
  isIdOrNull(value.from) &&
  isIdOrNull(value.to) &&
  isIdListOrNull(value.ids) &&
  isIdOrNull(value.next) &&
  typeof value.done === "boolean";

/**
 * @param {string} bodyText the body of a start or stop the device answered with 200
 * @returns {object | null} the `scan` object it carries; null when the body is no whole scan object, which only
 *   means the page learns the scan from the next poll
 */
export function scanReply(bodyText) {
  try {
    const scan = JSON.parse(bodyText);
    if (isScanObject(scan)) {
      return scan;
    }
  } catch {
    // Not JSON: no scan object.
  }
  return null;
}

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs: `goals` names the goals as the device does
 * @param {{goal: string, from: number | null, to: number | null} | null} scan the reply to an accepted start
 * @param {string} fromText the discover range's from, as sent
 * @param {string} toText the discover range's to, as sent
 * @returns {{from: number, to: number} | null} the discover range the device runs, to remember for the next start;
 *   null when the reply runs no discover range or another range than the one sent
 */
export function acceptedRange(catalogue, scan, fromText, toText) {
  if (scan?.goal !== catalogue.goals.discover || !Number.isInteger(scan.from) || !Number.isInteger(scan.to)) {
    return null;
  }
  if (scan.from !== wholeDecimal(fromText) || scan.to !== wholeDecimal(toText)) {
    return null;
  }
  return { from: scan.from, to: scan.to };
}
