/**
 * Delta polling of /sniffer/data (see .specs/sniffer.md, "Transport: delta polling").
 */

/** The page polls this often while the device answers. */
export const POLL_INTERVAL_MS = 1000;

/** After a failed poll, the page retries this much later and shows itself offline. */
export const OFFLINE_RETRY_MS = 5000;

/**
 * @param {object} copy the browser copy (see records.mjs)
 * @returns {number} the `since` of the next poll: 0, a full response, until this page load has polled
 */
export function sinceFor(copy) {
  return copy.since ?? 0;
}

/**
 * A delta the page can't apply: the page has not polled yet, the device rebooted, or its `seq` went below the
 * page's `since`. The page then polls again with `since=0`; a full response runs the reboot merge.
 *
 * @param {object} copy the browser copy
 * @param {object} response the parsed /sniffer/data JSON
 * @returns {string | null} why the page must resync, or null to apply the response
 */
export function resyncReason(copy, response) {
  if (response.full) {
    return null;
  }
  if (copy.since === null) {
    return "first load";
  }
  if (response.boot !== copy.boot) {
    return "boot changed";
  }
  if (response.seq < copy.since) {
    return "seq went down";
  }
  return null;
}

/**
 * Runs `poll` over and over, never two at once: 1 s after each answer, 5 s after each failure.
 *
 * @param {object} deps
 * @param {() => Promise<void>} deps.poll one poll; rejects when the device did not answer
 * @param {(fn: () => void, ms: number) => unknown} deps.setTimer schedules the next poll
 * @param {(timer: unknown) => void} deps.clearTimer cancels it
 * @param {(online: boolean) => void} deps.onOnline told after every poll whether the device answered
 * @returns {{start: () => void, pollNow: () => void}}
 */
export function createPoller({ poll, setTimer, clearTimer, onOnline }) {
  let inFlight = false;
  let pollAgain = false;
  let timer = null;

  async function run() {
    timer = null;
    inFlight = true;
    let delayMs = POLL_INTERVAL_MS;
    try {
      await poll();
      onOnline(true);
    } catch {
      onOnline(false);
      delayMs = OFFLINE_RETRY_MS;
    }
    inFlight = false;
    if (pollAgain) {
      pollAgain = false;
      run();
      return;
    }
    timer = setTimer(run, delayMs);
  }

  return {
    start: run,
    /** Polls at once, or right after the poll in flight; used after a scan start or stop. */
    pollNow() {
      if (inFlight) {
        pollAgain = true;
        return;
      }
      if (timer !== null) {
        clearTimer(timer);
      }
      run();
    },
  };
}
