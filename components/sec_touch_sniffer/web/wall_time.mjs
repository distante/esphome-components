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

/**
 * The device's uptime at a browser time, extrapolated from a response.
 *
 * @param {{browserNowMs: number, uptimeMs: number}} anchor browser time a response arrived and the uptime it carried
 * @param {number} browserMs a browser time (epoch ms)
 * @returns {number} the device's uptime then (ms)
 */
export function uptimeAtMs(anchor, browserMs) {
  return anchor.uptimeMs + (browserMs - anchor.browserNowMs);
}
