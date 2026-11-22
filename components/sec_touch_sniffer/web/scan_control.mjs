/**
 * The scan control's requests and button (see .specs/sniffer.md, "Goal switch", "Scan control" and the start
 * parameters of "Device/page contract"). DOM-free: the page sends the requests and renders the text.
 */

import { isObject } from "./records.mjs";

/** Shown for a refused start whose body carries no text. */
export const RANGE_REFUSED = "the device refused the range";

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
 * @param {{active: boolean, goal: string} | null} scan the device's scan state, null before it answered
 * @param {string} goal the open tab's goal
 * @returns {string} the start/stop button's text: a start names the open tab's goal, a stop the running scan's
 */
export function scanButtonLabel(scan, goal) {
  return scan?.active ? `■ Stop ${scan.goal} scan` : `▶ Start ${goal} scan`;
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
 * @param {string} bodyText the body of a start the device answered with 400
 * @returns {string} the text to show next to the range: the device's `error`, else the body, else a fallback
 */
export function rangeError(bodyText) {
  try {
    const body = JSON.parse(bodyText);
    if (typeof body?.error === "string") {
      return body.error;
    }
  } catch {
    // Not JSON: the body itself is the best text there is.
  }
  return bodyText.trim() || RANGE_REFUSED;
}

/**
 * @param {string} bodyText the body of a start or stop the device answered with 200
 * @returns {{active: boolean, goal: string} | null} the `scan` object it carries; null when the body is no scan
 *   object, which only means the page learns the scan from the next poll
 */
export function scanReply(bodyText) {
  try {
    const scan = JSON.parse(bodyText);
    if (isObject(scan) && typeof scan.active === "boolean" && typeof scan.goal === "string") {
      return scan;
    }
  } catch {
    // Not JSON: no scan object.
  }
  return null;
}

/** A range bound as typed, as a number; null when it is not plain digits. */
const typedBound = (text) => (/^\d+$/.test(text) ? Number(text) : null);

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs: `goals` names the goals as the device does
 * @param {{goal: string, from: number | null, to: number | null} | null} scan the reply to an accepted start
 * @param {string} fromText the discover range's from, as sent
 * @param {string} toText the discover range's to, as sent
 * @returns {{from: number, to: number} | null} the discover range the device runs, to remember for the next start;
 *   null when the reply runs no discover or another range than the one sent: a start while a scan runs is a no-op
 *   that replies with the running scan
 */
export function acceptedRange(catalogue, scan, fromText, toText) {
  if (scan?.goal !== catalogue.goals.discover || !Number.isInteger(scan.from) || !Number.isInteger(scan.to)) {
    return null;
  }
  if (scan.from !== typedBound(fromText) || scan.to !== typedBound(toText)) {
    return null;
  }
  return { from: scan.from, to: scan.to };
}
