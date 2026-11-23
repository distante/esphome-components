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
    catalogue.clockId];
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
  // Unreachable: isKnownId() lists exactly the IDs that have a branch above.
  return null;
}

/** What a known property shows while it has no value. */
export const NOT_SEEN = "not seen";

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
