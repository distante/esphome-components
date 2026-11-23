/**
 * This page's capture in flight (.specs/sniffer.md, "Experiments", Captures): what each poll means for it. DOM-free.
 *
 * A capture in flight is `{experimentKey, experimentName, label, next, boot}`: the experiment it goes to, the label
 * as typed, what it scans (experiments.mjs, `nextCapture`) and the device's `boot` when the start was accepted. It
 * lives in page memory only: a reload abandons it.
 */

import { ascending, deviceUptimeMs } from "./records.mjs";
import { runIds } from "./scan_progress.mjs";

const sameIds = (a, b) => a.length === b.length && a.every((id, i) => id === b[i]);

/** Whether `scan` is the run `next` asked for. */
function isRunOf(catalogue, scan, next) {
  if (scan.goal !== catalogue.goals.discover) {
    return false;
  }
  if (next.full) {
    return scan.ids === null && scan.from === next.from && scan.to === next.to;
  }
  return Array.isArray(scan.ids) && sameIds([...scan.ids].sort(ascending), next.ids);
}

/** An entry per ID the run queried: its query status and, when answered, its raw value. */
function entriesOf(catalogue, copy, scan) {
  const entries = {};
  for (const id of runIds(catalogue, scan)) {
    const record = copy.records[id];
    if (record === undefined || record.status === "unqueried") {
      continue;
    }
    entries[id] = { outcome: record.status, raw: record.status === "ok" ? record.raw : null };
  }
  return entries;
}

/**
 * A 200 to the start means the next run that ends is this page's own: once it ends, done stores the capture and a stop
 * stores nothing. A boot change (a reboot, or Clear data on any page) abandons it even while the device's run goes on.
 *
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {object} capture this page's capture in flight
 * @param {object} copy the browser copy after the poll (see records.mjs)
 * @param {boolean} fresh whether the poll was sent after the device accepted the start: an older one shows the run
 *   before it
 * @param {number} browserNowMs browser time (epoch ms)
 * @returns {{kind: "wait"} | {kind: "abandon", why: "boot changed" | "stopped" | "another run"} | {kind: "store",
 *   capture: object, range: {from: number, to: number} | null}} what to do; `store` carries the capture for
 *   experiments.mjs's `withCapture` and the full range it ran, null for a narrowed one
 */
export function captureStep(catalogue, capture, copy, fresh, browserNowMs) {
  const scan = copy.device?.scan ?? null;
  if (!fresh || scan === null) {
    return { kind: "wait" };
  }
  if (copy.boot !== capture.boot) {
    return { kind: "abandon", why: "boot changed" };
  }
  if (scan.active) {
    return { kind: "wait" };
  }
  if (!scan.done) {
    return { kind: "abandon", why: "stopped" };
  }
  if (!isRunOf(catalogue, scan, capture.next)) {
    return { kind: "abandon", why: "another run" };
  }
  return {
    kind: "store",
    range: capture.next.full ? { from: capture.next.from, to: capture.next.to } : null,
    capture: {
      label: capture.label,
      atMs: browserNowMs,
      uptimeMs: deviceUptimeMs(copy, browserNowMs),
      boot: copy.boot,
      full: capture.next.full,
      entries: entriesOf(catalogue, copy, scan),
    },
  };
}
