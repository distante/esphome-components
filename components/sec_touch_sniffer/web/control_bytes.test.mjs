import { test } from "node:test";
import assert from "node:assert/strict";

import { escapeControlBytes, shown } from "./control_bytes.mjs";

test("control bytes are written as two-digit lowercase \\xNN", () => {
  assert.equal(escapeControlBytes("a\x00b\x1fc\x7f"), "a\\x00b\\x1fc\\x7f");
  assert.equal(escapeControlBytes("\n\t\x1b"), "\\x0a\\x09\\x1b");
});

test("printable text, space and non-ASCII letters stay as they are", () => {
  assert.equal(escapeControlBytes("unexpected value 0A 00"), "unexpected value 0A 00");
  assert.equal(escapeControlBytes("Küche ~"), "Küche ~");
  assert.equal(escapeControlBytes(""), "");
});

test("a value never received is shown as a dash", () => {
  assert.equal(shown(null), "—");
});

test("a shown value is trimmed and its control bytes escaped", () => {
  assert.equal(shown("  13:47\x01 "), escapeControlBytes("13:47\x01"));
});
