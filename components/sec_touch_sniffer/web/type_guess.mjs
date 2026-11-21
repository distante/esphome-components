/**
 * Guessed types of raw values (see .specs/sniffer.md, "Type guessing"). Runs in the browser only; the device
 * serves raw strings.
 */

/** Every type a value or an ID can be guessed as, in the order the page lists them. */
export const TYPES = ["int", "padded int", "decimal", "date", "time", "hex", "text or hex", "text", "empty"];

const HEX_DIGITS = /^[0-9A-Fa-f]+$/;

/** Per value, the first matching rule wins. */
const VALUE_RULES = [
  ["empty", (s) => s === "" || /^-+$/.test(s)],
  ["date", (s) => /^\d\d\.\d\d\.\d\d$/.test(s)],
  ["time", (s) => /^\d\d:\d\d(:\d\d)?$/.test(s)],
  ["decimal", (s) => /^-?\d+\.\d+$/.test(s)],
  ["padded int", (s) => /^0\d+$/.test(s)],
  ["int", (s) => /^-?\d+$/.test(s)],
  ["text or hex", (s) => HEX_DIGITS.test(s)],
  ["text", () => true],
];

/**
 * @param {string | null} raw a raw value, padding included, or null when never seen
 * @returns {string | null} the type of the trimmed value, or null for a never-seen value
 */
export function guessValueType(raw) {
  if (raw == null) {
    return null;
  }
  const trimmed = raw.trim();
  return VALUE_RULES.find(([, matches]) => matches(trimmed))[0];
}

/**
 * @param {(string | null)[]} values every raw value an ID has had
 * @returns {string | null} the type of the ID across those values, or null when it has none
 */
export function guessIdType(values) {
  const seen = values.filter((value) => value != null);
  if (seen.length === 0) {
    return null;
  }
  const nonEmpty = seen.filter((value) => guessValueType(value) !== "empty");
  if (nonEmpty.length === 0) {
    return "empty";
  }
  const types = new Set(nonEmpty.map(guessValueType));
  if (types.size === 1) {
    return [...types][0];
  }
  if (types.has("text or hex") && nonEmpty.every((value) => HEX_DIGITS.test(value.trim()))) {
    return "hex";
  }
  if ([...types].every((type) => type === "int" || type === "padded int")) {
    return "padded int";
  }
  return "text";
}
