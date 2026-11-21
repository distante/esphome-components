/**
 * What the page keeps in browser storage (see .specs/sniffer.md, "Browser storage"): per device MAC, its browser
 * copy, notes and feed mutes, plus the MAC of the last device that answered. DOM-free: the page only reads and
 * writes the strings.
 */

import { emptyCopy, fromStored, isObject, toStored } from "./records.mjs";

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

const ID_KEY = /^\d+$/;

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
 * @returns {{copy: object, notes: Object<string, string>, mutes: Set<number>}} what it holds
 */
export function parseStored(text) {
  const stored = text === null ? null : parseJson(text);
  if (!isObject(stored)) {
    return { copy: emptyCopy(), notes: {}, mutes: new Set() };
  }
  const notes = isObject(stored.notes)
    ? Object.fromEntries(
        Object.entries(stored.notes).filter(([id, note]) => ID_KEY.test(id) && typeof note === "string"),
      )
    : {};
  const mutes =
    Array.isArray(stored.mutes) && stored.mutes.every(Number.isInteger) ? new Set(stored.mutes) : new Set();
  return { copy: fromStored(stored.copy ?? null), notes, mutes };
}

/**
 * @param {object} copy the browser copy (see records.mjs)
 * @param {Object<string, string>} notes per-ID notes
 * @param {Set<number>} mutes IDs left out of the feed
 * @returns {string} what browser storage keeps for the device
 */
export function storedText(copy, notes, mutes) {
  return JSON.stringify({ copy: toStored(copy), notes, mutes: [...mutes] });
}
