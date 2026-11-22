/**
 * The scan control's requests and button (see .specs/sniffer.md, "Goal switch", "Scan control" and the start
 * parameters of "Device/page contract"). DOM-free: the page sends the requests and renders the text.
 */

/** The goals as the device names them in `scan.goal` and the start's `goal` parameter. */
export const GOAL_DISCOVER = "discover";
export const GOAL_CHECK = "check";

/** Shown for a refused start whose body carries no text. */
const RANGE_REFUSED = "the device refused the range";

/**
 * The device decides whether a range is valid, so the range goes out as typed.
 *
 * @param {string} goal GOAL_DISCOVER or GOAL_CHECK
 * @param {string} fromText the discover range's from, as typed
 * @param {string} toText the discover range's to, as typed
 * @returns {string} the start request's path and query; check sends no range
 */
export function scanStartPath(goal, fromText, toText) {
  const path = `/sniffer/scan/start?goal=${encodeURIComponent(goal)}`;
  if (goal !== GOAL_DISCOVER) {
    return path;
  }
  return `${path}&from=${encodeURIComponent(fromText)}&to=${encodeURIComponent(toText)}`;
}

/**
 * @param {{active: boolean, goal: string} | null} scan the device's scan state, null before it answered
 * @param {string} goal the open tab's goal
 * @returns {string} the start/stop button's text: a start names the open tab's goal, a stop names the running
 *   scan's goal when it is another one
 */
export function scanButtonLabel(scan, goal) {
  if (!scan?.active) {
    return `▶ Start ${goal} scan`;
  }
  return scan.goal === goal ? "■ Stop scan" : `■ Stop ${scan.goal} scan`;
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
 * @param {{goal: string, from: number | null, to: number | null} | null} scan the reply to an accepted start
 * @returns {{from: number, to: number} | null} the discover range the device runs, to remember for the next start;
 *   null when the reply is no discover run (a start while a check runs is a no-op that replies with the check)
 */
export function acceptedRange(scan) {
  if (scan?.goal !== GOAL_DISCOVER || !Number.isInteger(scan.from) || !Number.isInteger(scan.to)) {
    return null;
  }
  return { from: scan.from, to: scan.to };
}
