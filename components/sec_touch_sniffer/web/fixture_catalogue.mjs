/**
 * Test fixture only, never imported by the page: a small catalogue in the shape catalogue.mjs has (page_bundle.py),
 * with made-up IDs so the `node --test` suites don't lean on the real ones.
 */

export const FIXTURE_CATALOGUE = {
  levelIds: [10, 11],
  labelIds: [20, 21],
  summerVentilationId: 30,
  dateId: 31,
  clockId: 32,
  levelNames: { 0: "off", 3: "level 3", 7: "Burst", 255: "not connected" },
  labelNames: ["-", "Bad", "Küche"],
  summerVentilationNames: { "0A00": "on", "0800": "off" },
  discoverRange: { from: 1, to: 255 },
  goals: { discover: "discover", check: "check" },
};
