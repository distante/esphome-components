/**
 * The check goal's cards (.specs/sniffer.md, "Check"): one per fan and a Global card, each property with its raw
 * value and expected meaning. DOM-free: the page renders the cards.
 */

import { knownMeaning } from "./known_meaning.mjs";

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {Map<number, object>} rowsById the rows by ID
 * @param {number} id a known property ID
 * @returns {{id: number, raw: string | null, meaning: {text: string, unexpected: boolean} | null}} the property;
 *   `raw` and `meaning` are null while it has no value (not seen)
 */
function knownProperty(catalogue, rowsById, id) {
  const raw = rowsById.get(id)?.raw ?? null;
  return { id, raw, meaning: knownMeaning(catalogue, id, raw) };
}

/**
 * @param {object} catalogue the CATALOGUE of catalogue.mjs
 * @param {object[]} rows the rows of rowsOf() (records.mjs); only `id`, `raw` and `registered` are read
 * @returns {{fans: object[], global: object[]}} `fans`: per fan in fan order, `{fan, registered, level, label}`, a fan
 *   registered when the hub registered its level or label; `global`: summer ventilation, date and clock as
 *   `{name, ...property}`. Each property is `{id, raw, meaning}`, see knownProperty().
 */
export function checkCards(catalogue, rows) {
  const rowsById = new Map(rows.map((row) => [row.id, row]));
  const fans = catalogue.levelIds.map((levelId, i) => {
    const labelId = catalogue.labelIds[i];
    return {
      fan: i + 1,
      registered: Boolean(rowsById.get(levelId)?.registered || rowsById.get(labelId)?.registered),
      level: knownProperty(catalogue, rowsById, levelId),
      label: knownProperty(catalogue, rowsById, labelId),
    };
  });
  const global = [
    ["summer ventilation", catalogue.summerVentilationId],
    ["date", catalogue.dateId],
    ["clock", catalogue.clockId],
  ].map(([name, id]) => ({ name, ...knownProperty(catalogue, rowsById, id) }));
  return { fans, global };
}
