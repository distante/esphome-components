/**
 * Known properties and the expected meaning of their raw values (.specs/sniffer.md, "Known-property catalogue").
 * The page passes the generated CATALOGUE (catalogue.mjs, built by page_bundle.py; its known properties come from
 * sec_touch's protocol_ids.h and fan_level.h);
 * the rules match known_property_meaning() in components/sec_touch/known_property_meaning.h.
 */

import { ascending, wholeDecimal } from "./records.mjs";
import { escapeControlBytes, shown } from "./control_bytes.mjs";

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @returns {number[]} every known property ID, ascending: the IDs a check scan queries, in its order
 */
export function knownIds(catalogue) {
  const ids = [...catalogue.levelIds, ...catalogue.labelIds, catalogue.summerVentilationId, catalogue.dateId,
    catalogue.clockId, ...catalogue.timeProgramEnabledIds, ...catalogue.timeProgramTimeIds,
    ...catalogue.timeProgramLevelIds];
  return [...new Set(ids)].sort(ascending);
}

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {number} id a property ID
 * @returns {boolean} whether the ID is a known property
 */
export function isKnownId(catalogue, id) {
  return knownIds(catalogue).includes(id);
}

// ASCII whitespace only, matching the trim of known_property_meaning() in C++. The page's display trim
// (String.prototype.trim) is wider: it also drops Unicode spaces.
const ASCII_SPACE_AT_ENDS = /^[ \t\n\v\f\r]+|[ \t\n\v\f\r]+$/g;

/** @returns {string | undefined} the name the trimmed raw, read as a whole decimal, has in `names` */
function nameAtWholeDecimal(names, trimmed) {
  const value = wholeDecimal(trimmed);
  if (value === null) {
    return undefined;
  }
  const key = String(value);
  return Object.hasOwn(names, key) ? names[key] : undefined;
}

/** @returns {string | undefined} a switch time's level name: a level name up to the highest switch-time level */
function switchTimeLevelName(catalogue, trimmed) {
  const level = wholeDecimal(trimmed);
  if (level === null || level > catalogue.maxSwitchTimeLevel) {
    return undefined;
  }
  return nameAtWholeDecimal(catalogue.levelNames, trimmed);
}

const TWO_HEX_DIGITS = /^[0-9A-Fa-f]{2}$/;

/** @returns {number | null} the trimmed raw as an enabled mask: 2 hex digits, a bit per switch time and none above */
function enabledMask(catalogue, trimmed) {
  if (!TWO_HEX_DIGITS.test(trimmed)) {
    return null;
  }
  const mask = parseInt(trimmed, 16);
  return mask < 2 ** catalogue.switchTimeCount ? mask : null;
}

/** @returns {boolean} whether switch time `switchTime` (from 1) has its bit, N-1, set in `mask` */
const hasSwitchTimeBit = (mask, switchTime) => (mask & (1 << (switchTime - 1))) !== 0;

/**
 * @returns {string | undefined} the name of an enabled mask: the enabled switch times' numbers, ascending and
 *   separated, then the suffix, or the none name for no bit
 */
function enabledMaskName(catalogue, trimmed) {
  const mask = enabledMask(catalogue, trimmed);
  if (mask === null) {
    return undefined;
  }
  const names = catalogue.enabledMaskNames;
  if (mask === 0) {
    return names.none;
  }
  const enabled = [];
  for (let n = 1; n <= catalogue.switchTimeCount; n++) {
    if (hasSwitchTimeBit(mask, n)) {
      enabled.push(n);
    }
  }
  return enabled.join(names.separator) + names.suffix;
}

const HH_MM_SS = /^([0-9]{2}):([0-9]{2}):([0-9]{2})$/;
/** @returns {boolean} whether the trimmed raw is exactly HH:MM:SS, each part below its catalogue.timeUnits bound */
function isSwitchTimeTime(catalogue, trimmed) {
  const match = HH_MM_SS.exec(trimmed);
  if (match === null) {
    return false;
  }
  const [hour, minute, second] = match.slice(1).map(Number);
  const units = catalogue.timeUnits;
  return hour < units.hoursPerDay && minute < units.minutesPerHour && second < units.secondsPerMinute;
}

function nameOrUnexpected(name, trimmed) {
  return name === undefined
    ? { text: `unexpected value ${trimmed}`, unexpected: true }
    : { text: name, unexpected: false };
}

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {number} id a property ID
 * @param {string | null} raw the raw value as received, null when never seen
 * @returns {{ text: string, unexpected: boolean } | null} the meaning, or null for an unknown ID or a never-seen value
 */
export function knownMeaning(catalogue, id, raw) {
  if (raw === null || !isKnownId(catalogue, id)) {
    return null;
  }
  const trimmed = raw.replace(ASCII_SPACE_AT_ENDS, "");
  if (catalogue.levelIds.includes(id)) {
    return nameOrUnexpected(nameAtWholeDecimal(catalogue.levelNames, trimmed), trimmed);
  }
  if (catalogue.labelIds.includes(id)) {
    return nameOrUnexpected(nameAtWholeDecimal(catalogue.labelNames, trimmed), trimmed);
  }
  if (id === catalogue.summerVentilationId) {
    const names = catalogue.summerVentilationNames;
    return nameOrUnexpected(Object.hasOwn(names, trimmed) ? names[trimmed] : undefined, trimmed);
  }
  if (id === catalogue.dateId || id === catalogue.clockId) {
    return { text: trimmed, unexpected: false };
  }
  if (catalogue.timeProgramLevelIds.includes(id)) {
    return nameOrUnexpected(switchTimeLevelName(catalogue, trimmed), trimmed);
  }
  if (catalogue.timeProgramEnabledIds.includes(id)) {
    return nameOrUnexpected(enabledMaskName(catalogue, trimmed), trimmed);
  }
  if (catalogue.timeProgramTimeIds.includes(id)) {
    return nameOrUnexpected(isSwitchTimeTime(catalogue, trimmed) ? trimmed : undefined, trimmed);
  }
  // Unreachable: isKnownId() lists exactly the IDs that have a branch above.
  return null;
}

/** What a known property shows while it has no value. */
export const NOT_SEEN = "not seen";

/** What a switch time's enabled cell shows for its bit of the pair's enabled mask. */
export const SWITCH_TIME_ENABLED = "yes";
export const SWITCH_TIME_NOT_ENABLED = "no";

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {string | null} raw the pair's enabled mask as received, null when never seen
 * @param {number} switchTime the switch time, counted from 1
 * @returns {{ text: string, unexpected: boolean }} whether the switch time is enabled; unexpected while the mask is
 *   not seen (NOT_SEEN) or the mask rule rejects it (`unexpected value x`)
 */
export function switchTimeEnabled(catalogue, raw, switchTime) {
  if (raw === null) {
    return { text: NOT_SEEN, unexpected: true };
  }
  const trimmed = raw.replace(ASCII_SPACE_AT_ENDS, "");
  const mask = enabledMask(catalogue, trimmed);
  if (mask === null) {
    return nameOrUnexpected(undefined, trimmed);
  }
  return nameOrUnexpected(hasSwitchTimeBit(mask, switchTime) ? SWITCH_TIME_ENABLED : SWITCH_TIME_NOT_ENABLED, trimmed);
}

/**
 * @param {{ text: string, unexpected: boolean } | null} meaning a meaning of knownMeaning(), null for none
 * @returns {string} the meaning as shown: its text with control bytes as \xNN, or NOT_SEEN for none
 */
export function meaningText(meaning) {
  return meaning === null ? NOT_SEEN : escapeControlBytes(meaning.text);
}

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {number} id a known property ID
 * @param {string | null} raw the raw value as received, null when never seen
 * @returns {string} the tooltip of the ID's cell in discover's map under "show known": `ID = value · meaning`
 */
export function knownCellTitle(catalogue, id, raw) {
  return `${id} = ${shown(raw)} · ${meaningText(knownMeaning(catalogue, id, raw))}`;
}
