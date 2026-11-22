/** Control bytes in a value shown on the page. Runs in the browser only. */

/**
 * @param {string} text any text, such as a trimmed raw value
 * @returns {string} the text with each control byte (0x00-0x1f, 0x7f) written as \xNN
 */
export function escapeControlBytes(text) {
  return text.replace(/[\x00-\x1f\x7f]/g, (c) => `\\x${c.charCodeAt(0).toString(16).padStart(2, "0")}`);
}
