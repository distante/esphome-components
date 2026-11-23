/**
 * Test fixture only, never imported by the page: records as /sniffer/data serves them (.specs/sniffer.md,
 * "JSON shape").
 */

/** The `boot` of the fixture device. */
export const FIXTURE_BOOT = "9f2c41ab";

/** The device uptime at which a fixture record was seen, unless a test gives its own `seenAt`. */
const SEEN_AT = 1000;

/**
 * A record with `raw`, seen once at `seenAt` and answered ok then; a null raw is a record that only ever failed.
 * `fields` replace any of its fields.
 */
export function deviceRecord(id, raw, { seenAt = SEEN_AT, ...fields } = {}) {
  const seen = raw === null ? null : seenAt;
  return {
    id,
    cmd: raw === null ? null : 32,
    raw,
    first: seen,
    last: seen,
    changed: seen,
    changes: 0,
    status: raw === null ? "nak" : "ok",
    status_at: seenAt,
    reg: false,
    hist: raw === null ? [] : [[seenAt, raw]],
    ...fields,
  };
}

/** A full /sniffer/data response carrying `records`; `fields` replace any of its envelope fields. */
export function deviceResponse(records, fields = {}) {
  return {
    mac: "aabbccddeeff",
    name: "ventilation",
    boot: FIXTURE_BOOT,
    uptime: 10_000,
    seq: 7,
    full: true,
    dropped: 0,
    scan: null,
    records,
    ...fields,
  };
}
