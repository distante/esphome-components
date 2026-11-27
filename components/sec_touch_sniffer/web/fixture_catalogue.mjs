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
  // Two pairs of two switch times each: enabled mask, times, levels per pair, as in protocol_ids.h.
  timeProgramEnabledIds: [60, 65],
  timeProgramTimeIds: [61, 62, 66, 67],
  timeProgramLevelIds: [63, 64, 68, 69],
  switchTimeCount: 2,
  maxSwitchTimeLevel: 7,
  enabledMaskNames: { none: "none enabled", separator: ", ", suffix: " enabled" },
  levelNames: { 0: "off", 3: "level 3", 7: "Burst", 255: "not connected" },
  labelNames: ["-", "Bad", "Küche"],
  summerVentilationNames: { "0A00": "on", "0800": "off" },
  discoverRange: { from: 1, to: 255 },
  goals: { discover: "discover", check: "check" },
  scanListCap: 3,
  probeValueCap: 4,
  probeForbiddenBytes: [0x00, 0x01, 0x1b],
};
