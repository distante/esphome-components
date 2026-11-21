/**
 * The page's filters (see .specs/sniffer.md, "Page"). They dim non-matching cells and apply to the change feed.
 */

import { TYPES } from "./type_guess.mjs";

/** The query statuses, as the device names them. */
export const STATUSES = ["unqueried", "ok", "nak", "timeout"];

/** @returns {object} a filter every row passes */
export function allPass() {
  return { query: "", types: new Set(TYPES), statuses: new Set(STATUSES), hideRegistered: false, changedWithinMs: 0 };
}

/**
 * @param {object} row a page row (see records.mjs, `rowsOf`)
 * @param {{query: string, types: Set<string>, statuses: Set<string>, hideRegistered: boolean,
 *   changedWithinMs: number}} filter `changedWithinMs` 0 means any time
 * @param {number} nowMs browser time (epoch ms)
 * @returns {boolean} whether the row passes every filter
 */
export function matchesFilter(row, filter, nowMs) {
  if (row.type !== null && !filter.types.has(row.type)) {
    return false;
  }
  if (!filter.statuses.has(row.status)) {
    return false;
  }
  if (filter.hideRegistered && row.registered) {
    return false;
  }
  if (filter.changedWithinMs > 0) {
    const changedRecently = row.changeCount > 0 && nowMs - row.changedAtMs < filter.changedWithinMs;
    if (!changedRecently) {
      return false;
    }
  }
  return matchesQuery(row, filter.query.trim().toLowerCase());
}

function matchesQuery(row, query) {
  if (query === "") {
    return true;
  }
  if (String(row.id) === query) {
    return true;
  }
  if (row.raw !== null && row.raw.trim().toLowerCase().includes(query)) {
    return true;
  }
  return row.note.toLowerCase().includes(query);
}
