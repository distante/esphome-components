/**
 * Discover's filters (see .specs/sniffer.md, "Discover"). They dim non-matching cells and apply to the change feed,
 * all but "only changed since mark" (see `feedIds`).
 * Known properties are never visible in discover, whatever the filter.
 */

import { isKnownId } from "./known_meaning.mjs";
import { changedSinceMark } from "./mark.mjs";
import { STATUSES } from "./records.mjs";
import { TYPES } from "./type_guess.mjs";

/** @returns {object} a filter every row passes */
export function allPass() {
  return {
    query: "",
    types: new Set(TYPES),
    statuses: new Set(STATUSES),
    changedWithinMs: 0,
    onlyChangedSinceMark: false,
  };
}

/**
 * @param {object} row a page row (see records.mjs, `rowsOf`)
 * @param {{query: string, types: Set<string>, statuses: Set<string>, changedWithinMs: number,
 *   onlyChangedSinceMark: boolean}} filter `changedWithinMs` 0 means any time
 * @param {number} nowMs browser time (epoch ms)
 * @param {object | null} mark the mark (see mark.mjs); `onlyChangedSinceMark` lets every row through without one
 * @returns {boolean} whether the row passes every filter
 */
export function matchesFilter(row, filter, nowMs, mark) {
  if (row.type !== null && !filter.types.has(row.type)) {
    return false;
  }
  if (!filter.statuses.has(row.status)) {
    return false;
  }
  if (filter.changedWithinMs > 0) {
    const changedRecently = row.changeCount > 0 && nowMs - row.changedAtMs < filter.changedWithinMs;
    if (!changedRecently) {
      return false;
    }
  }
  if (filter.onlyChangedSinceMark && mark !== null && !changedSinceMark(mark, row.id, row.raw)) {
    return false;
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

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {object[]} rows the page rows (see records.mjs, `rowsOf`)
 * @param {object} filter the filter, as for `matchesFilter`
 * @param {number} nowMs browser time (epoch ms)
 * @param {object | null} mark the mark (see mark.mjs), null when none is set
 * @returns {Set<number>} the IDs discover shows undimmed: unknown ones that pass the filter
 */
export function visibleIds(catalogue, rows, filter, nowMs, mark) {
  const visible = rows.filter((row) => !isKnownId(catalogue, row.id) && matchesFilter(row, filter, nowMs, mark));
  return new Set(visible.map((row) => row.id));
}

/**
 * The change feed leaves out "only changed since mark": the mark diff can't show a value toggled away and back,
 * so the feed must still list those changes (.specs/sniffer.md, "Discover").
 *
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {object[]} rows the page rows (see records.mjs, `rowsOf`)
 * @param {object} filter the filter, as for `matchesFilter`
 * @param {number} nowMs browser time (epoch ms)
 * @param {object | null} mark the mark (see mark.mjs), null when none is set
 * @returns {Set<number>} the IDs whose changes the feed lists
 */
export function feedIds(catalogue, rows, filter, nowMs, mark) {
  return visibleIds(catalogue, rows, { ...filter, onlyChangedSinceMark: false }, nowMs, mark);
}
