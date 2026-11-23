/**
 * Test fixture only, never imported by the page: experiments and captures as experiments.mjs stores them
 * (.specs/sniffer.md, "Experiments").
 */

/** An entry of a capture whose query got NAK. */
export const NAK = { outcome: "nak", raw: null };

/** An entry of a capture whose query timed out. */
export const TIMEOUT = { outcome: "timeout", raw: null };

/** The browser time of the first fixture capture; each further one is a minute later. */
const FIRST_CAPTURE_MS = Date.UTC(2026, 10, 22, 9, 0, 0);
const MINUTE_MS = 60_000;

/**
 * A capture labelled `label` with one entry per key of `values`: a string is an ok answer with that raw value,
 * NAK and TIMEOUT are those outcomes.
 */
export function capture(label, values, { index = 0, full = true, boot = "9f2c41ab" } = {}) {
  const entries = Object.fromEntries(
    Object.entries(values).map(([id, value]) => [id, typeof value === "string" ? { outcome: "ok", raw: value } : value]),
  );
  return { label, atMs: FIRST_CAPTURE_MS + index * MINUTE_MS, uptimeMs: 5000 + index * MINUTE_MS, boot, full, entries };
}

/**
 * An experiment holding one capture per `[label, values]` pair, in that order (see `capture`).
 */
export function experimentWith(pairs, { name = "fan 1 mode", range = { from: 1, to: 255 } } = {}) {
  return {
    key: "e1",
    name,
    createdAtMs: FIRST_CAPTURE_MS - MINUTE_MS,
    range,
    captures: pairs.map(([label, values], index) => capture(label, values, { index })),
    probes: [],
  };
}

/** One ID's walk: an experiment whose captures give ID `id` the `[label, value]` pairs in order. */
export function walkOf(id, pairs) {
  return experimentWith(pairs.map(([label, value]) => [label, { [id]: value }]));
}
