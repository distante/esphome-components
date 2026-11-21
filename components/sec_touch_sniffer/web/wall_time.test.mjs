import { test } from "node:test";
import assert from "node:assert/strict";

import { uptimeAtMs, wallTimeMs } from "./wall_time.mjs";

test("a record seen 5 s of uptime ago shows 5 s before the browser's now", () => {
  const browserNowMs = Date.UTC(2026, 10, 16, 12, 0, 0);
  const uptimeNowMs = 3_600_000;
  const recordUptimeMs = 3_595_000;

  assert.equal(
    wallTimeMs(browserNowMs, uptimeNowMs, recordUptimeMs),
    Date.UTC(2026, 10, 16, 11, 59, 55),
  );
});

test("2 s of browser time after a response, the device's uptime is 2 s further on", () => {
  const anchor = { browserNowMs: Date.UTC(2026, 10, 16, 12, 0, 0), uptimeMs: 3_600_000 };

  assert.equal(uptimeAtMs(anchor, Date.UTC(2026, 10, 16, 12, 0, 2)), 3_602_000);
});
