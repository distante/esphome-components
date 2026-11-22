/**
 * The mark (.specs/sniffer.md, "Discover"): a snapshot of every raw value, to show what changed since.
 * Page memory only: never stored in the browser, and the device knows nothing of it.
 *
 * A mark is `{setAtMs, uptimeMs, raws}`: the browser time it was set, the device uptime then (null when no device
 * answered on this page load), and the raw value per property ID of the browser copy then.
 */

import { isKnownId } from "./known_meaning.mjs";
import { deviceUptimeMs } from "./records.mjs";

/**
 * @param {object} copy the browser copy (see records.mjs)
 * @param {number} browserNowMs browser time (epoch ms)
 * @returns {object} a mark of every raw value of the copy now
 */
export function markNow(copy, browserNowMs) {
  const raws = Object.fromEntries(Object.values(copy.records).map((record) => [record.id, record.raw]));
  return { setAtMs: browserNowMs, uptimeMs: deviceUptimeMs(copy, browserNowMs), raws };
}

/**
 * @param {object | null} mark the mark, null when none is set
 * @param {number} id a property ID
 * @returns {string | null} the ID's raw value at the mark; null without a mark or when it had no value then
 */
export function markRaw(mark, id) {
  if (mark === null) {
    return null;
  }
  return mark.raws[id] ?? null;
}

/**
 * @param {object | null} mark the mark, null when none is set
 * @param {number} id a property ID
 * @param {string | null} raw its raw value now, null when it has none
 * @returns {boolean} whether the raw value differs from the mark's; no value at the mark and one now is a change
 */
export function changedSinceMark(mark, id, raw) {
  return mark !== null && markRaw(mark, id) !== raw;
}

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {object} copy the browser copy (see records.mjs)
 * @param {object | null} mark the mark, null when none is set
 * @returns {{id: number, markRaw: string | null, raw: string | null}[]} the unknown IDs changed since the mark, by ID
 */
export function markChanges(catalogue, copy, mark) {
  if (mark === null) {
    return [];
  }
  const rawNow = (id) => copy.records[id]?.raw ?? null;
  const ids = new Set([...Object.keys(mark.raws), ...Object.keys(copy.records)].map(Number));
  return [...ids]
    .filter((id) => !isKnownId(catalogue, id) && changedSinceMark(mark, id, rawNow(id)))
    .sort((a, b) => a - b)
    .map((id) => ({ id, markRaw: markRaw(mark, id), raw: rawNow(id) }));
}
