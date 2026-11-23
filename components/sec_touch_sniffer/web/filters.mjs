/**
 * Discover's filters (see .specs/sniffer.md, "Discover"). They dim non-matching cells and apply to the change feed,
 * all but "only candidates" (see `feedIds`).
 * Known properties are never visible in discover, whatever the filter.
 */

import { isKnownId } from "./known_meaning.mjs";
import { STATUSES } from "./records.mjs";
import { TYPES } from "./type_guess.mjs";

/** @returns {object} a filter every row passes */
export function allPass() {
  return {
    query: "",
    types: new Set(TYPES),
    statuses: new Set(STATUSES),
    changedWithinMs: 0,
    onlyCandidates: false,
  };
}

/**
 * @param {object} row a page row (see records.mjs, `rowsOf`)
 * @param {{query: string, types: Set<string>, statuses: Set<string>, changedWithinMs: number,
 *   onlyCandidates: boolean}} filter `changedWithinMs` 0 means any time
 * @param {number} nowMs browser time (epoch ms)
 * @param {Set<number> | null} candidates the open experiment's candidates and corroborated candidates; null with no
 *   experiment open, when `onlyCandidates` lets every row through
 * @returns {boolean} whether the row passes every filter
 */
export function matchesFilter(row, filter, nowMs, candidates) {
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
  if (filter.onlyCandidates && candidates !== null && !candidates.has(row.id)) {
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
 * @param {Set<number> | null} candidates the open experiment's candidate IDs, null with no experiment open
 * @returns {Set<number>} the IDs discover shows undimmed: unknown ones that pass the filter
 */
export function visibleIds(catalogue, rows, filter, nowMs, candidates) {
  const visible = rows.filter(
    (row) => !isKnownId(catalogue, row.id) && matchesFilter(row, filter, nowMs, candidates),
  );
  return new Set(visible.map((row) => row.id));
}

/**
 * The change feed leaves out "only candidates" (.specs/sniffer.md, "Discover"): it keeps the raw changes the
 * candidate rules do not explain.
 *
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {object[]} rows the page rows (see records.mjs, `rowsOf`)
 * @param {object} filter the filter, as for `matchesFilter`
 * @param {number} nowMs browser time (epoch ms)
 * @returns {Set<number>} the IDs whose changes the feed lists
 */
export function feedIds(catalogue, rows, filter, nowMs) {
  // No candidate set: "only candidates" lets every row through.
  return visibleIds(catalogue, rows, filter, nowMs, null);
}
