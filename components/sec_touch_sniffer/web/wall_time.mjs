/**
 * Wall-clock time of a record, from device uptime (see .specs/sniffer.md, "Timestamps").
 * The device has no RTC, so the page anchors uptime to the browser's clock.
 *
 * @param {number} browserNowMs browser time when the device response arrived (epoch ms)
 * @param {number} uptimeNowMs device uptime at that response (ms)
 * @param {number} recordUptimeMs device uptime when the record was captured (ms)
 * @returns {number} wall time of the record (epoch ms)
 */
export function wallTimeMs(browserNowMs, uptimeNowMs, recordUptimeMs) {
  return browserNowMs - (uptimeNowMs - recordUptimeMs);
}
