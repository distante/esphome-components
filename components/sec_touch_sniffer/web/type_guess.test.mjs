import { test } from "node:test";
import assert from "node:assert/strict";

import { guessIdType, guessValueType } from "./type_guess.mjs";

// Raw values from a real scan (components/sec_touch/RAW_VALUES.md) and the spec's examples.
test("each raw value gets the first type whose rule it matches", () => {
  const cases = [
    ["", "empty"],
    ["   ", "empty"],
    ["-----------", "empty"],
    ["11.11.26", "date"],
    ["00.00.00", "date"],
    ["13:32", "time"],
    ["13:32:08", "time"],
    ["-50.0", "decimal"],
    ["0100", "padded int"],
    ["01", "padded int"],
    ["00               ", "padded int"],
    ["0", "int"],
    ["1200", "int"],
    ["-5", "int"],
    ["0A00", "text or hex"],
    ["SWZ0021E21H", "text"],
    ["0.360.00:00", "text"],
  ];
  for (const [raw, type] of cases) {
    assert.equal(guessValueType(raw), type, JSON.stringify(raw));
  }
});

test("a never-seen raw value has no type", () => {
  assert.equal(guessValueType(null), null);
});

test("an ID whose non-empty values agree takes their type", () => {
  assert.equal(guessIdType(["11.11.26", "00.00.00", "-----------"]), "date");
});

test("an ID with only hex digits and one text-or-hex value is hex", () => {
  assert.equal(guessIdType(["0A00", "0800"]), "hex");
});

test("an ID with any text value is text", () => {
  assert.equal(guessIdType(["0A00", "ZZ"]), "text");
});

test("an ID mixing int and padded int is padded int", () => {
  assert.equal(guessIdType(["0100", "80"]), "padded int");
});

test("an ID mixing any other types is text", () => {
  assert.equal(guessIdType(["11.11.26", "13:32:08"]), "text");
});

test("an ID with only empty values is empty, and one with no value has no type", () => {
  assert.equal(guessIdType(["-----------", ""]), "empty");
  assert.equal(guessIdType([]), null);
});
