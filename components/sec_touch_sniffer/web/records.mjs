/**
 * The page's browser copy of a device's property records, fed by /sniffer/data responses
 * (see .specs/sniffer.md, "Device/page contract" and "Browser storage").
 *
 * A copy is `{boot, since, device, records}`. An `anchor` is `{browserNowMs, uptimeMs}`: the browser time a response
 * arrived and the device uptime it carried. `device` is the latest response's identity and scan, plus its `anchor`.
 * `records` maps a property ID to the record as the device served it, plus `anchor` (the response it came in, to
 * read its uptimes as wall time) and `stale` (only in the browser copy since the device rebooted).
 */

import { guessIdType } from "./type_guess.mjs";
import { wallTimeMs } from "./wall_time.mjs";

/** @returns {object} a copy that has seen no response yet */
export function emptyCopy() {
  return { boot: null, since: null, device: null, records: {} };
}

/**
 * Applies one /sniffer/data response. A delta replaces the records it carries, whole. A full response does the
 * same and marks every ID it lacks as stale: after a reboot the device wins for its IDs and the rest stay.
 *
 * @param {object} copy the browser copy, left unchanged
 * @param {object} response the parsed /sniffer/data JSON
 * @param {number} browserNowMs browser time when the response arrived (epoch ms)
 * @returns {{copy: object, changedIds: number[]}} the new copy, and the IDs whose raw value changed
 */
export function applyResponse(copy, response, browserNowMs) {
  const anchor = { browserNowMs, uptimeMs: response.uptime };
  const records = { ...copy.records };
  if (response.full) {
    for (const id of Object.keys(records)) {
      records[id] = { ...records[id], stale: true };
    }
  }
  const changedIds = [];
  for (const record of response.records) {
    const previous = copy.records[record.id];
    if (previous !== undefined && previous.raw !== record.raw) {
      changedIds.push(record.id);
    }
    records[record.id] = { ...record, anchor, stale: false };
  }
  const device = { mac: response.mac, name: response.name, scan: response.scan, anchor };
  return { copy: { boot: response.boot, since: response.seq, device, records }, changedIds };
}

/**
 * @param {object} record a record of the copy
 * @param {number | null} uptimeMs one of its device uptimes
 * @returns {number | null} that uptime as wall time (epoch ms), or null when unset
 */
export function wallOf(record, uptimeMs) {
  if (uptimeMs == null) {
    return null;
  }
  return wallTimeMs(record.anchor.browserNowMs, record.anchor.uptimeMs, uptimeMs);
}

/** @returns {object} what browser storage keeps of a copy */
export function toStored(copy) {
  return { boot: copy.boot, device: copy.device, records: copy.records };
}

/** @returns {boolean} whether the value is a plain JSON object (not null, not an array) */
export const isObject = (value) => value !== null && typeof value === "object" && !Array.isArray(value);

const hasAnchor = (value) =>
  isObject(value) &&
  isObject(value.anchor) &&
  typeof value.anchor.browserNowMs === "number" &&
  typeof value.anchor.uptimeMs === "number";

const isHistoryEntry = (entry) =>
  Array.isArray(entry) && entry.length === 2 && typeof entry[0] === "number" && typeof entry[1] === "string";

const isReadableRecord = (record) =>
  hasAnchor(record) &&
  typeof record.id === "number" &&
  (record.raw === null || typeof record.raw === "string") &&
  typeof record.status === "string" &&
  typeof record.changes === "number" &&
  typeof record.reg === "boolean" &&
  Array.isArray(record.hist) &&
  record.hist.every(isHistoryEntry);

/**
 * Old or corrupted storage must not break the page: unreadable records and an unreadable device are dropped.
 * The stored scan state is dropped too: it is not live, so the scan control waits for the device to answer.
 *
 * @param {unknown} stored what browser storage held, parsed
 * @returns {object} the copy it holds, polling from scratch (`since` unset), or an empty copy when unreadable
 */
export function fromStored(stored) {
  if (!isObject(stored) || !isObject(stored.records)) {
    return emptyCopy();
  }
  const records = Object.fromEntries(Object.entries(stored.records).filter(([, record]) => isReadableRecord(record)));
  const device = hasAnchor(stored.device) ? { ...stored.device, scan: null } : null;
  return { boot: stored.boot ?? null, since: null, device, records };
}

/**
 * The copy as the page shows and exports it: one row per ID, in ID order, with wall times, guessed type and note.
 *
 * @param {object} copy the browser copy
 * @param {Object<string, string>} notes per-ID notes
 * @returns {object[]} the rows
 */
export function rowsOf(copy, notes) {
  return Object.values(copy.records)
    .sort((a, b) => a.id - b.id)
    .map((record) => {
      const history = record.hist.map(([uptimeMs, raw]) => ({ raw, uptimeMs, atMs: wallOf(record, uptimeMs) }));
      // Without the history pool, `hist` is empty and the current value is the only one known.
      const values = history.length > 0 ? history.map((entry) => entry.raw) : [record.raw];
      return {
        id: record.id,
        cmd: record.cmd,
        raw: record.raw,
        type: guessIdType(values),
        registered: record.reg,
        status: record.status,
        statusAtMs: wallOf(record, record.status_at),
        changeCount: record.changes,
        firstSeenUptimeMs: record.first,
        firstSeenMs: wallOf(record, record.first),
        lastSeenMs: wallOf(record, record.last),
        changedUptimeMs: record.changed,
        changedAtMs: wallOf(record, record.changed),
        history,
        stale: record.stale,
        note: notes[record.id] ?? "",
      };
    });
}

/**
 * @param {object} copy the browser copy
 * @param {Set<number>} mutedIds IDs left out of the feed
 * @returns {{id: number, atMs: number, from: string, to: string}[]} every change in the histories, newest first
 */
export function changeFeed(copy, mutedIds) {
  const feed = [];
  for (const record of Object.values(copy.records)) {
    if (mutedIds.has(record.id)) {
      continue;
    }
    // `hist` is newest first: each entry changed from the one after it.
    for (let i = 0; i + 1 < record.hist.length; i++) {
      const [at, to] = record.hist[i];
      feed.push({ id: record.id, atMs: wallOf(record, at), from: record.hist[i + 1][1], to });
    }
  }
  return feed.sort((a, b) => b.atMs - a.atMs);
}
