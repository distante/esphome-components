import { test } from "node:test";
import assert from "node:assert/strict";

import { wallTimeMs } from "./wall_time.mjs";

test("a record seen 5 s of uptime ago shows 5 s before the browser's now", () => {
  const browserNowMs = Date.UTC(2026, 10, 16, 12, 0, 0);
  const uptimeNowMs = 3_600_000;
  const recordUptimeMs = 3_595_000;

  assert.equal(
    wallTimeMs(browserNowMs, uptimeNowMs, recordUptimeMs),
    Date.UTC(2026, 10, 16, 11, 59, 55),
  );
});
