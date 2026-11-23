/**
 * JSON and CSV export of the browser copy, notes and experiments (see .specs/sniffer.md, "Export").
 *
 * Raw values stay strings of one code point per byte, as the device's `\u00XX` escapes deliver them, so the JSON
 * export recovers the exact bytes.
 */

import { analyseExperiment, excludedFromExperiments } from "./experiments.mjs";
import { isKnownId, knownMeaning } from "./known_meaning.mjs";
import { deviceUptimeMs, rowsOf } from "./records.mjs";
import { runningProbe } from "./write_probe.mjs";

const isoOf = (wallMs) => (wallMs === null ? null : new Date(wallMs).toISOString());

/** The meaning text, as check's cards show it; null for an unknown ID and a known one with no value. */
const meaningOf = (catalogue, row) => knownMeaning(catalogue, row.id, row.raw)?.text ?? null;

/**
 * What both exports take besides the copy and the notes.
 *
 * @typedef {object} ExportOptions
 * @property {Set<number>} mutes the muted IDs
 * @property {boolean} includeMuted "include muted IDs": muted IDs stay in records and capture entries, flagged
 * @property {object[]} experiments every stored experiment (see experiments.mjs)
 * @property {string | null} openExperimentKey the open experiment's key, null when none is open
 * @property {boolean} onlyOpen "only the open experiment": narrows `experiments` to the open one
 * @property {object | null} pendingProbe the probe or restore awaiting its result (see write_probe.mjs)
 */

/** The rows to export: muted ones only when included, then each with its `muted` flag. */
function exportedRows(copy, notes, { mutes, includeMuted }) {
  return rowsOf(copy, notes).filter((row) => includeMuted || !mutes.has(row.id));
}

/** A capture's entries: muted IDs left out, or kept with a `muted` flag on every entry. */
function exportedEntries(entries, { mutes, includeMuted }) {
  const exported = {};
  for (const [id, entry] of Object.entries(entries)) {
    const muted = mutes.has(Number(id));
    if (muted && !includeMuted) {
      continue;
    }
    exported[id] = includeMuted ? { ...entry, muted } : entry;
  }
  return exported;
}

/** A probe entry or running probe object with `at`, the wall time it was started, instead of `atMs`. */
const exportedProbe = ({ atMs, ...probe }) => ({ ...probe, at: isoOf(atMs) });

function exportedExperiment(catalogue, experiment, deviceProbe, options) {
  const excluded = excludedFromExperiments(catalogue, options.mutes);
  const { candidates, disqualified } = analyseExperiment(experiment, excluded);
  const probes = experiment.probes.map(exportedProbe);
  const pending = options.pendingProbe;
  if (pending !== null && pending.experimentKey === experiment.key) {
    probes.push(exportedProbe({ ...runningProbe(pending, deviceProbe), atMs: pending.atMs }));
  }
  return {
    name: experiment.name,
    from: experiment.range?.from ?? null,
    to: experiment.range?.to ?? null,
    created_at: isoOf(experiment.createdAtMs),
    captures: experiment.captures.map((capture) => ({
      label: capture.label,
      at: isoOf(capture.atMs),
      uptime_ms: capture.uptimeMs,
      boot: capture.boot,
      full: capture.full,
      entries: exportedEntries(capture.entries, options),
    })),
    candidates,
    disqualified,
    probes,
  };
}

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {object} copy the browser copy (see records.mjs)
 * @param {Object<string, string>} notes per-ID notes
 * @param {ExportOptions} options mutes and experiments
 * @param {number} browserNowMs browser time of the export (epoch ms)
 * @returns {string} the JSON export
 */
export function exportJson(catalogue, copy, notes, options, browserNowMs) {
  const device = copy.device;
  const records = exportedRows(copy, notes, options).map((row) => ({
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
    ...(options.includeMuted ? { muted: options.mutes.has(row.id) } : {}),
  }));
  const experiments = options.onlyOpen
    ? options.experiments.filter((experiment) => experiment.key === options.openExperimentKey)
    : options.experiments;
  const deviceProbe = device?.probe ?? null;
  return JSON.stringify(
    {
      exported_at: new Date(browserNowMs).toISOString(),
      device: device === null ? null : { mac: device.mac, name: device.name, boot: copy.boot },
      uptime_ms: deviceUptimeMs(copy, browserNowMs),
      scan: device === null ? null : device.scan,
      records,
      experiments: experiments.map((experiment) => exportedExperiment(catalogue, experiment, deviceProbe, options)),
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
];

/** Always quoted: a raw value keeps its padding, and any of them may hold commas or quotes. */
const quoted = (text) => `"${(text ?? "").replaceAll('"', '""')}"`;

/**
 * Goal, range, experiments and write probes are JSON only; probe results reach the CSV through `note`.
 *
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {object} copy the browser copy (see records.mjs)
 * @param {Object<string, string>} notes per-ID notes
 * @param {ExportOptions} options only `mutes` and `includeMuted` are read
 * @returns {string} the CSV export (RFC 4180: CRLF line ends)
 */
export function exportCsv(catalogue, copy, notes, options) {
  const columns = options.includeMuted ? [...CSV_COLUMNS, "muted"] : CSV_COLUMNS;
  const lines = [columns.join(",")];
  for (const row of exportedRows(copy, notes, options)) {
    const cells = [
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
    ];
    if (options.includeMuted) {
      cells.push(options.mutes.has(row.id));
    }
    lines.push(cells.join(","));
  }
  return lines.join("\r\n");
}
