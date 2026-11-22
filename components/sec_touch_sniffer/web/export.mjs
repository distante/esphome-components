/**
 * JSON and CSV export of the browser copy, notes included (see .specs/sniffer.md, "Export").
 *
 * Raw values stay strings of one code point per byte, as the device's `\u00XX` escapes deliver them, so the JSON
 * export recovers the exact bytes.
 */

import { isKnownId, knownMeaning } from "./known_meaning.mjs";
import { markRaw } from "./mark.mjs";
import { deviceUptimeMs, rowsOf } from "./records.mjs";

const isoOf = (wallMs) => (wallMs === null ? null : new Date(wallMs).toISOString());

/** The meaning text, as check's cards show it; null for an unknown ID and a known one with no value. */
const meaningOf = (catalogue, row) => knownMeaning(catalogue, row.id, row.raw)?.text ?? null;

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {object} copy the browser copy (see records.mjs)
 * @param {Object<string, string>} notes per-ID notes
 * @param {object | null} mark the mark (see mark.mjs), null when none is set
 * @param {number} browserNowMs browser time of the export (epoch ms)
 * @returns {string} the JSON export
 */
export function exportJson(catalogue, copy, notes, mark, browserNowMs) {
  const device = copy.device;
  const records = rowsOf(copy, notes).map((row) => ({
    id: row.id,
    cmd: row.cmd,
    raw: row.raw,
    guessed_type: row.type,
    registered: row.registered,
    known: isKnownId(catalogue, row.id),
    meaning: meaningOf(catalogue, row),
    status: row.status,
    change_count: row.changeCount,
    first_seen: { uptime_ms: row.firstSeenUptimeMs, wall: isoOf(row.firstSeenMs) },
    last_changed: { uptime_ms: row.changedUptimeMs, wall: isoOf(row.changedAtMs) },
    history: row.history.map((entry) => ({ raw: entry.raw, uptime_ms: entry.uptimeMs, wall: isoOf(entry.atMs) })),
    stale: row.stale,
    note: row.note,
    mark_raw: markRaw(mark, row.id),
  }));
  return JSON.stringify(
    {
      exported_at: new Date(browserNowMs).toISOString(),
      device: device === null ? null : { mac: device.mac, name: device.name, boot: copy.boot },
      uptime_ms: deviceUptimeMs(copy, browserNowMs),
      scan: device === null ? null : device.scan,
      mark: mark === null ? null : { set_at: isoOf(mark.setAtMs), uptime_ms: mark.uptimeMs },
      records,
    },
    null,
    2,
  );
}

const CSV_COLUMNS = [
  "id",
  "raw",
  "guessed_type",
  "registered",
  "known",
  "meaning",
  "status",
  "change_count",
  "first_seen",
  "last_changed",
  "note",
  "mark_raw",
];

/** Always quoted: a raw value keeps its padding, and any of them may hold commas or quotes. */
const quoted = (text) => `"${(text ?? "").replaceAll('"', '""')}"`;

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {object} copy the browser copy (see records.mjs)
 * @param {Object<string, string>} notes per-ID notes
 * @param {object | null} mark the mark (see mark.mjs), null when none is set
 * @returns {string} the CSV export (RFC 4180: CRLF line ends)
 */
export function exportCsv(catalogue, copy, notes, mark) {
  const lines = [CSV_COLUMNS.join(",")];
  for (const row of rowsOf(copy, notes)) {
    lines.push(
      [
        row.id,
        quoted(row.raw),
        row.type ?? "",
        row.registered,
        isKnownId(catalogue, row.id),
        quoted(meaningOf(catalogue, row)),
        row.status,
        row.changeCount,
        isoOf(row.firstSeenMs) ?? "",
        isoOf(row.changedAtMs) ?? "",
        quoted(row.note),
        quoted(markRaw(mark, row.id)),
      ].join(","),
    );
  }
  return lines.join("\r\n");
}
