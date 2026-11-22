/**
 * The badges of discover's drawer header (.specs/sniffer.md, "Discover", Drawer): which to show, as plain data.
 */

import { TYPE_COLOURS } from "./colours.mjs";

/** The text of the badge a stale row shows: only in the browser copy since the device rebooted. */
export const STALE_BADGE_TEXT = "stale: browser copy only";

/**
 * @param {{ type: string | null, stale: boolean }} row a row of rowsOf() (records.mjs)
 * @returns {{ text: string, colour: string | null }[]} the badges to show, in order: the guessed type in its colour
 *   of TYPE_COLOURS when there is one, then STALE_BADGE_TEXT, uncoloured, when the row is stale
 */
export function drawerBadges(row) {
  const badges = [];
  if (row.type) {
    badges.push({ text: row.type, colour: TYPE_COLOURS[row.type] });
  }
  if (row.stale) {
    badges.push({ text: STALE_BADGE_TEXT, colour: null });
  }
  return badges;
}
