/**
 * The page's cell and legend colours, one per guessed type and one per query status.
 */

/** A colour per type of TYPES (type_guess.mjs). */
export const TYPE_COLOURS = {
  int: "#9cc3ea",
  "padded int": "#6fa3d8",
  decimal: "#8fd4ae",
  date: "#f0b48a",
  time: "#c9a2e6",
  hex: "#f39ac2",
  "text or hex": "#f2c4d8",
  text: "#bbb",
  empty: "#fff",
};

/** A colour per status of STATUSES (filters.mjs). */
export const STATUS_COLOURS = { ok: "#8fd4ae", nak: "#e88b80", timeout: "#f5cf74", unqueried: "#c9d3df" };
