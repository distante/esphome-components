/**
 * JSON and CSV export of the browser copy, notes included (see .specs/sniffer.md, "Export").
 *
 * Raw values stay strings of one code point per byte, as the device's `\u00XX` escapes deliver them, so the JSON
 * export recovers the exact bytes.
 */

import { rowsOf } from "./records.mjs";
import { uptimeAtMs } from "./wall_time.mjs";

const isoOf = (wallMs) => (wallMs === null ? null : new Date(wallMs).toISOString());

/**
 * @param {object} copy the browser copy (see records.mjs)
 * @param {Object<string, string>} notes per-ID notes
 * @param {number} browserNowMs browser time of the export (epoch ms)
 * @returns {string} the JSON export
 */
export function exportJson(copy, notes, browserNowMs) {
  const device = copy.device;
  const records = rowsOf(copy, notes).map((row) => ({
    id: row.id,
    cmd: row.cmd,
    raw: row.raw,
    guessed_type: row.type,
    registered: row.registered,
    status: row.status,
    change_count: row.changeCount,
    first_seen: { uptime_ms: row.firstSeenUptimeMs, wall: isoOf(row.firstSeenMs) },
    last_changed: { uptime_ms: row.changedUptimeMs, wall: isoOf(row.changedAtMs) },
    history: row.history.map((entry) => ({ raw: entry.raw, uptime_ms: entry.uptimeMs, wall: isoOf(entry.atMs) })),
    stale: row.stale,
    note: row.note,
  }));
  return JSON.stringify(
    {
      exported_at: new Date(browserNowMs).toISOString(),
      device: device === null ? null : { mac: device.mac, name: device.name, boot: copy.boot },
      // Only extrapolated from a response of this page load: a stored anchor may predate a reboot.
      uptime_ms: device === null || copy.since === null ? null : uptimeAtMs(device.anchor, browserNowMs),
      scan: device === null ? null : device.scan,
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
  "status",
  "change_count",
  "first_seen",
  "last_changed",
  "note",
];

/** Always quoted: a raw value keeps its padding, and either may hold commas or quotes. */
const quoted = (text) => `"${(text ?? "").replaceAll('"', '""')}"`;

/**
 * @param {object} copy the browser copy (see records.mjs)
 * @param {Object<string, string>} notes per-ID notes
 * @returns {string} the CSV export (RFC 4180: CRLF line ends)
 */
export function exportCsv(copy, notes) {
  const lines = [CSV_COLUMNS.join(",")];
  for (const row of rowsOf(copy, notes)) {
    lines.push(
      [
        row.id,
        quoted(row.raw),
        row.type ?? "",
        row.registered,
        row.status,
        row.changeCount,
        isoOf(row.firstSeenMs) ?? "",
        isoOf(row.changedAtMs) ?? "",
        quoted(row.note),
      ].join(","),
    );
  }
  return lines.join("\r\n");
}
