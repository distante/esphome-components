/**
 * The scan control's label and progress bar, from the `scan` object of /sniffer/data
 * (see .specs/sniffer.md, "Page" and "JSON shape").
 */

/**
 * @param {{active: boolean, goal: "discover" | "check", from: number | null, to: number | null, next: number | null,
 *   done: boolean}} scan the device's scan state; `from`/`to` are null for check and before any run, `next` is null
 *   before any run and after a completed one
 * @returns {{label: string, percent: number}} the label and how much of the range is behind the scan (0–100)
 */
export function scanProgress(scan) {
  if (scan.next === null && !scan.done) {
    return { label: "Not scanned yet", percent: 0 };
  }
  if (scan.done) {
    return { label: `Scan of ${scan.from}–${scan.to} complete`, percent: 100 };
  }
  const percent = Math.round((100 * (scan.next - scan.from)) / (scan.to - scan.from + 1));
  if (scan.active) {
    return { label: `Scanning ID ${scan.next} of ${scan.from}–${scan.to}`, percent };
  }
  return { label: `Stopped at ID ${scan.next}`, percent };
}
