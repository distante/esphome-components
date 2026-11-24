/**
 * What the page keeps in browser storage (see .specs/sniffer.md, "Browser storage"): per device MAC, its browser
 * copy, notes, mutes, last discover range, experiments and pending probe, plus the MAC of the last device that
 * answered. DOM-free: the page only reads and writes the strings.
 */

import { allPass } from "./filters.mjs";
import { emptyCopy, fromStored, isObject, isStringOrNull, OUTCOMES, toStored, wholeDecimal } from "./records.mjs";
import { isPendingProbe, isProbeEntry } from "./write_probe.mjs";

/** Every key the page stores starts with this. */
export const STORAGE_PREFIX = "sec_touch_sniffer:";

/** The key of the last device's MAC. A MAC is hex, so it never collides with a device key. */
export const LAST_MAC_KEY = `${STORAGE_PREFIX}last_mac`;

/**
 * @param {string} mac a device MAC
 * @returns {string} the key of that device's copy, notes and mutes
 */
export function deviceKey(mac) {
  return `${STORAGE_PREFIX}${mac}`;
}

/** The device decides which ranges are valid: a stored range only has to be two integers. */
const isRange = (range) => isObject(range) && Number.isInteger(range.from) && Number.isInteger(range.to);

/** A capture's entry: a query outcome, and a raw value or null. */
const isEntry = (entry) =>
  isObject(entry) && OUTCOMES.includes(entry.outcome) && isStringOrNull(entry.raw);

const isCapture = (capture) =>
  isObject(capture) &&
  typeof capture.label === "string" &&
  Number.isFinite(capture.atMs) &&
  (capture.uptimeMs === null || Number.isFinite(capture.uptimeMs)) &&
  isStringOrNull(capture.boot) &&
  typeof capture.full === "boolean" &&
  isObject(capture.entries) &&
  Object.entries(capture.entries).every(([id, entry]) => wholeDecimal(id) !== null && isEntry(entry));

/**
 * @param {unknown} experiment a stored experiment
 * @returns {object | null} the experiment with its unreadable captures and probes dropped, or null when the
 *   experiment itself is unreadable
 */
function readExperiment(experiment) {
  const readable =
    isObject(experiment) &&
    typeof experiment.key === "string" &&
    typeof experiment.name === "string" &&
    Number.isFinite(experiment.createdAtMs) &&
    (experiment.range === null || isRange(experiment.range)) &&
    Array.isArray(experiment.captures) &&
    Array.isArray(experiment.probes);
  if (!readable) {
    return null;
  }
  return {
    ...experiment,
    captures: experiment.captures.filter(isCapture),
    probes: experiment.probes.filter(isProbeEntry),
  };
}

function parseJson(text) {
  try {
    return JSON.parse(text);
  } catch {
    return null;
  }
}

/**
 * Old or corrupted storage must not break the page: each part that is unreadable is dropped on its own.
 *
 * @param {string | null} text what browser storage held for a device, or null when nothing
 * @param {{from: number, to: number}} defaultRange the discover range when none is stored: the catalogue's
 * @returns {{copy: object, notes: Object<string, string>, mutes: Set<number>, range: {from: number, to: number},
 *   experiments: object[], pendingProbe: object | null}} what it holds (see experiments.mjs and write_probe.mjs)
 */
export function parseStored(text, defaultRange) {
  const stored = text === null ? null : parseJson(text);
  if (!isObject(stored)) {
    return {
      copy: emptyCopy(),
      notes: {},
      mutes: new Set(),
      range: defaultRange,
      experiments: [],
      pendingProbe: null,
    };
  }
  const notes = isObject(stored.notes)
    ? Object.fromEntries(
        Object.entries(stored.notes).filter(([id, note]) => wholeDecimal(id) !== null && typeof note === "string"),
      )
    : {};
  const mutes =
    Array.isArray(stored.mutes) && stored.mutes.every(Number.isInteger) ? new Set(stored.mutes) : new Set();
  const range = isRange(stored.range) ? { from: stored.range.from, to: stored.range.to } : defaultRange;
  const experiments = Array.isArray(stored.experiments)
    ? stored.experiments.map(readExperiment).filter((experiment) => experiment !== null)
    : [];
  const pendingProbe = isPendingProbe(stored.pendingProbe) ? stored.pendingProbe : null;
  return { copy: fromStored(stored.copy ?? null), notes, mutes, range, experiments, pendingProbe };
}

/**
 * @param {object} device what the page keeps for the device
 * @param {object} device.copy the browser copy (see records.mjs)
 * @param {Object<string, string>} device.notes per-ID notes
 * @param {Set<number>} device.mutes IDs left out of the feed and of experiments
 * @param {{from: number, to: number}} device.range the last discover range the device accepted
 * @param {object[]} device.experiments the experiments (see experiments.mjs)
 * @param {object | null} device.pendingProbe the probe or restore awaiting its result (see write_probe.mjs)
 * @returns {string} what browser storage keeps for the device
 */
export function storedText({ copy, notes, mutes, range, experiments, pendingProbe }) {
  return JSON.stringify({ copy: toStored(copy), notes, mutes: [...mutes], range, experiments, pendingProbe });
}

/** The key of the page's checkboxes, shared by every device. Not hex, so it never collides with a device key. */
export const OPTIONS_KEY = `${STORAGE_PREFIX}options`;

/**
 * The page's checkboxes, as the page starts and as "Reset options" restores them.
 *
 * @returns {{showKnown: boolean, includeMuted: boolean, onlyOpen: boolean, onlyCandidates: boolean,
 *   types: Set<string>, statuses: Set<string>}} `types` and `statuses` are the ticked filter chips
 */
export function defaultOptions() {
  const { onlyCandidates, types, statuses } = allPass();
  return { showKnown: false, includeMuted: false, onlyOpen: false, onlyCandidates, types, statuses };
}

/** A stored chip list, kept to the chips that still exist; null when it is not a list. */
const chipsOf = (stored, all) => (Array.isArray(stored) ? new Set(stored.filter((chip) => all.has(chip))) : null);

/**
 * @param {string | null} text what browser storage held under `OPTIONS_KEY`, or null when nothing
 * @returns {object} the checkboxes (see `defaultOptions`); each unreadable one falls back to its default
 */
export function parseOptions(text) {
  const defaults = defaultOptions();
  const stored = text === null ? null : parseJson(text);
  if (!isObject(stored)) {
    return defaults;
  }
  const flag = (name) => (typeof stored[name] === "boolean" ? stored[name] : defaults[name]);
  return {
    showKnown: flag("showKnown"),
    includeMuted: flag("includeMuted"),
    onlyOpen: flag("onlyOpen"),
    onlyCandidates: flag("onlyCandidates"),
    types: chipsOf(stored.types, defaults.types) ?? defaults.types,
    statuses: chipsOf(stored.statuses, defaults.statuses) ?? defaults.statuses,
  };
}

/**
 * @param {object} options the checkboxes (see `defaultOptions`)
 * @returns {string} what browser storage keeps under `OPTIONS_KEY`
 */
export function optionsText(options) {
  return JSON.stringify({ ...options, types: [...options.types], statuses: [...options.statuses] });
}
