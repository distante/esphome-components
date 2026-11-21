import { test } from "node:test";
import assert from "node:assert/strict";

import { createPoller, OFFLINE_RETRY_MS, POLL_INTERVAL_MS, resyncReason, sinceFor } from "./poll.mjs";

test("a page that has not polled yet asks for everything", () => {
  assert.equal(sinceFor({ boot: "9f2c41ab", since: null, records: {} }), 0);
  assert.equal(sinceFor({ boot: "9f2c41ab", since: 12, records: {} }), 12);
});

test("a delta to a page that has not polled yet needs a resync", () => {
  const copy = { boot: "9f2c41ab", since: null, records: {} };
  assert.equal(resyncReason(copy, { boot: "9f2c41ab", seq: 12, full: false }), "first load");
  assert.equal(resyncReason(copy, { boot: "9f2c41ab", seq: 12, full: true }), null);
});

test("a delta from another boot needs a resync", () => {
  const copy = { boot: "9f2c41ab", since: 12, records: {} };
  assert.equal(resyncReason(copy, { boot: "00000001", seq: 30, full: false }), "boot changed");
});

test("a delta whose seq is below the page's since needs a resync", () => {
  const copy = { boot: "9f2c41ab", since: 12, records: {} };
  assert.equal(resyncReason(copy, { boot: "9f2c41ab", seq: 11, full: false }), "seq went down");
});

test("a delta from the same boot at or past since, or any full response, is applied as is", () => {
  const copy = { boot: "9f2c41ab", since: 12, records: {} };
  assert.equal(resyncReason(copy, { boot: "9f2c41ab", seq: 12, full: false }), null);
  assert.equal(resyncReason(copy, { boot: "00000001", seq: 1, full: true }), null);
});

/** Timers the test fires by hand. */
function fakeTimers() {
  const pending = [];
  return {
    pending,
    setTimer(fn, ms) {
      const timer = { fn, ms };
      pending.push(timer);
      return timer;
    },
    clearTimer(timer) {
      const i = pending.indexOf(timer);
      if (i >= 0) {
        pending.splice(i, 1);
      }
    },
    async fireNext() {
      const timer = pending.shift();
      await timer.fn();
    },
  };
}

/** A poll the test settles by hand. */
function controlledPoll() {
  const calls = [];
  return {
    calls,
    poll() {
      return new Promise((resolve, reject) => {
        calls.push({ resolve, reject });
      });
    },
  };
}

const settle = () => new Promise((resolve) => setImmediate(resolve));

test("the next poll is scheduled 1 s after the previous one answers, never while it is in flight", async () => {
  const timers = fakeTimers();
  const device = controlledPoll();
  const poller = createPoller({ poll: device.poll, ...timers, onOnline: () => {} });

  poller.start();
  await settle();
  assert.equal(device.calls.length, 1);
  assert.equal(timers.pending.length, 0);

  device.calls[0].resolve();
  await settle();
  assert.deepEqual(
    timers.pending.map((t) => t.ms),
    [POLL_INTERVAL_MS],
  );
});

test("a failed poll backs off to 5 s and shows offline, and the next answer returns to 1 s and online", async () => {
  const timers = fakeTimers();
  const device = controlledPoll();
  const online = [];
  const poller = createPoller({ poll: device.poll, ...timers, onOnline: (isOnline) => online.push(isOnline) });

  poller.start();
  device.calls[0].reject(new Error("offline"));
  await settle();
  assert.deepEqual(
    timers.pending.map((t) => t.ms),
    [OFFLINE_RETRY_MS],
  );

  const retry = timers.fireNext();
  device.calls[1].resolve();
  await retry;
  await settle();
  assert.deepEqual(
    timers.pending.map((t) => t.ms),
    [POLL_INTERVAL_MS],
  );
  assert.deepEqual(online, [false, true]);
});

test("polling now while a poll is in flight runs one more poll right after it, not a second at once", async () => {
  const timers = fakeTimers();
  const device = controlledPoll();
  const poller = createPoller({ poll: device.poll, ...timers, onOnline: () => {} });

  poller.start();
  poller.pollNow();
  await settle();
  assert.equal(device.calls.length, 1);

  device.calls[0].resolve();
  await settle();
  assert.equal(device.calls.length, 2);
  assert.equal(timers.pending.length, 0);
});

test("polling now while idle cancels the scheduled poll and polls at once", async () => {
  const timers = fakeTimers();
  const device = controlledPoll();
  const poller = createPoller({ poll: device.poll, ...timers, onOnline: () => {} });

  poller.start();
  device.calls[0].resolve();
  await settle();
  poller.pollNow();
  await settle();

  assert.equal(device.calls.length, 2);
  assert.equal(timers.pending.length, 0);
});

/** Runs `body` with console.error captured instead of printed, and returns what it was called with. */
async function capturingConsoleError(body) {
  const original = console.error;
  const logged = [];
  console.error = (...args) => logged.push(args);
  try {
    await body();
  } finally {
    console.error = original;
  }
  return logged;
}

/** A poller whose onOnline records each call and then throws. */
function pollerWithThrowingOnOnline() {
  const timers = fakeTimers();
  const device = controlledPoll();
  const online = [];
  const poller = createPoller({
    poll: device.poll,
    ...timers,
    onOnline: (isOnline) => {
      online.push(isOnline);
      throw new Error("render failed");
    },
  });
  // The page never awaits start(); the assertions below check what a failing run would break.
  const start = () => Promise.resolve(poller.start()).catch(() => {});
  return { timers, device, online, start };
}

test("an onOnline that throws after a good poll is logged, and the next poll is still 1 s later", async () => {
  const { timers, device, online, start } = pollerWithThrowingOnOnline();

  const logged = await capturingConsoleError(async () => {
    start();
    device.calls[0].resolve();
    await settle();
  });

  assert.deepEqual(
    timers.pending.map((t) => t.ms),
    [POLL_INTERVAL_MS],
  );
  assert.deepEqual(online, [true]);
  assert.equal(logged.length, 1);
});

test("an onOnline that throws after a failed poll is logged, and the retry is still 5 s later", async () => {
  const { timers, device, online, start } = pollerWithThrowingOnOnline();

  const logged = await capturingConsoleError(async () => {
    start();
    device.calls[0].reject(new Error("offline"));
    await settle();
  });

  assert.deepEqual(
    timers.pending.map((t) => t.ms),
    [OFFLINE_RETRY_MS],
  );
  assert.deepEqual(online, [false]);
  assert.equal(logged.length, 1);
});
