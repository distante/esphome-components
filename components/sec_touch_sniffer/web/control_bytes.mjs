/** Control bytes in a value shown on the page. Runs in the browser only. */

/**
 * @param {string} text any text, such as a trimmed raw value
 * @returns {string} the text with each control byte (0x00-0x1f, 0x7f) written as \xNN
 */
export function escapeControlBytes(text) {
  return text.replace(/[\x00-\x1f\x7f]/g, (c) => `\\x${c.charCodeAt(0).toString(16).padStart(2, "0")}`);
}

/**
 * @param {string | null} raw a raw value as received, null when it has none
 * @returns {string} the value for display: trimmed, with control bytes as \xNN, or "—" for none
 */
export function shown(raw) {
  if (raw === null) {
    return "—";
  }
  return escapeControlBytes(raw.trim());
}
