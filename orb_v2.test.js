"use strict";

// Node-only test harness. Every Discord dependency is mocked; no requests are made.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const test = require("node:test");

const source = fs.readFileSync(path.join(__dirname, "orb_v2.js"), "utf8");

function loadOrb({ api, timers = globalThis } = {}) {
  const hooks = {};
  const requireCache = api ? { api: { exports: api } } : {};
  const webpackChunk = [];
  webpackChunk.push = entry => {
    entry[2]({ c: requireCache });
    return 1;
  };
  webpackChunk.pop = () => undefined;

  const window = { __orbV2TestHooks: hooks, webpackChunkdiscord_app: webpackChunk };
  const context = vm.createContext({
    AbortController,
    Date,
    Math,
    Number,
    Object,
    String,
    Array,
    Error,
    TypeError,
    Promise,
    console: { log() {}, warn() {}, error() {}, table() {} },
    setTimeout: timers.setTimeout.bind(timers),
    clearTimeout: timers.clearTimeout.bind(timers),
    window
  });
  vm.runInContext(source, context, { filename: "orb_v2.js" });
  return { hooks, orb: window.orbV2, window };
}

function immediateTimers() {
  return {
    setTimeout(callback) {
      queueMicrotask(callback);
      return 1;
    },
    clearTimeout() {}
  };
}

test("mock run completes and returns completed LIFO cleanup", async () => {
  const { orb } = loadOrb({ timers: immediateTimers() });
  const result = await orb.mock();

  assert.equal(result.summary.completed, true);
  assert.deepEqual(Array.from(result.cleanupTrace), ["unsubscribe-listener", "restore-store"]);
  assert.equal(orb.status().active, false);
});

test("stop aborts a mock while it is waiting", async () => {
  const { orb } = loadOrb();
  const run = orb.mock();
  assert.equal(orb.status().active, true);

  assert.equal(await orb.stop("test cancellation"), true);
  assert.equal(await run, null);
  assert.equal(orb.status().active, false);
});

test("duplicate runs are rejected while one run is active", async () => {
  const { orb } = loadOrb();
  const first = orb.mock();

  await assert.rejects(orb.mock(), /already running/);
  await orb.stop("duplicate-run test");
  assert.equal(await first, null);
});

test("thrown errors clear active state and cleanup is LIFO and idempotent", async () => {
  const { hooks, orb } = loadOrb();
  const cleanup = [];
  const context = new hooks.RunnerContext("failure test");
  context.own(() => cleanup.push("first"));
  context.own(() => cleanup.push("second"));

  try {
    throw new Error("simulated failure");
  } catch (error) {
    assert.match(error.message, /simulated failure/);
  } finally {
    await context.cleanup();
    await context.cleanup();
  }
  assert.deepEqual(cleanup, ["second", "first"]);
  await assert.rejects(orb.run({ mode: "unknown" }), /Unknown mode/);
  assert.equal(orb.status().active, false);
});

test("malformed quest data is safely represented without mutation", async () => {
  const api = { get: async () => ({ status: 200, body: { quests: [null, {}, "bad"] } }) };
  const { orb } = loadOrb({ api });
  const quests = await orb.inspect();

  assert.equal(quests.length, 3);
  assert.deepEqual(quests.map(quest => quest.id), [null, null, null]);
  assert.equal(orb.status().active, false);
});

test("429 responses stop after the bounded retry budget", async () => {
  let requests = 0;
  const api = {
    get: async () => {
      requests += 1;
      return { status: 429, body: { retry_after: 0 } };
    }
  };
  const { orb } = loadOrb({ api, timers: immediateTimers() });

  await assert.rejects(orb.inspect(), /retry budget/);
  assert.equal(requests, 9);
  assert.equal(orb.status().active, false);
});

test("missing Discord webpack fails clearly and leaves no active run", async () => {
  const { orb, window } = loadOrb();
  delete window.webpackChunkdiscord_app;

  await assert.rejects(orb.inspect(), /webpack chunk array was not found/);
  assert.equal(orb.status().active, false);
});

test("missing Discord API module fails clearly and leaves no active run", async () => {
  const { orb } = loadOrb();

  await assert.rejects(orb.inspect(), /Could not find Discord API client/);
  assert.equal(orb.status().active, false);
});
