"use strict";

// Isolated Node harness: Discord, webpack, timers, and HTTP are all mocked.
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
    return webpackChunk.length + 1;
  };
  webpackChunk.pop = () => undefined;

  const window = { __orbV2TestHooks: hooks, webpackChunkdiscord_app: webpackChunk };
  const context = vm.createContext({
    AbortController,
    AggregateError,
    Date,
    Math,
    Number,
    Object,
    String,
    Array,
    Error,
    TypeError,
    RangeError,
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

function heldTimers() {
  const callbacks = new Map();
  let nextId = 1;
  return {
    setTimeout(callback) {
      const id = nextId++;
      callbacks.set(id, callback);
      return id;
    },
    clearTimeout(id) {
      callbacks.delete(id);
    },
    pendingCount() {
      return callbacks.size;
    }
  };
}

function quest({ taskName = "WATCH_VIDEO", expiresAt, enrolled = true, completed = false } = {}) {
  const now = Date.now();
  return {
    id: "quest-1",
    config: {
      expires_at: expiresAt ?? new Date(now + 60_000).toISOString(),
      messages: { quest_name: "Fixture quest" },
      application: { name: "Fixture app" },
      task_config_v2: { tasks: { [taskName]: { target: 30 } } }
    },
    user_status: {
      enrolled_at: enrolled ? new Date(now - 1_000).toISOString() : null,
      completed_at: completed ? new Date(now - 500).toISOString() : null,
      progress: { [taskName]: { value: 3 } }
    }
  };
}

test("mock run completes and reports completed LIFO cleanup", async () => {
  const { orb } = loadOrb({ timers: immediateTimers() });
  const result = await orb.mock();

  assert.equal(result.summary.completed, true);
  assert.deepEqual(Array.from(result.cleanupTrace), ["unsubscribe-listener", "restore-store"]);
  assert.equal(orb.status().active, false);
});

test("duplicate starts are rejected; repeated stop is safe; a new run may start after cancellation", async () => {
  const { orb } = loadOrb();
  const firstRun = orb.mock();
  await assert.rejects(orb.mock(), /already running/);

  const stops = await Promise.all([orb.stop("first stop"), orb.stop("second stop")]);
  assert.deepEqual(stops, [true, true]);
  assert.equal(await firstRun, null);
  assert.equal(await orb.stop(), false);

  const secondRun = orb.mock();
  assert.equal(await orb.stop("restart test"), true);
  assert.equal(await secondRun, null);
  assert.equal(orb.status().active, false);
});

test("cleanup runs in LIFO order, records thrown cleanup failures, and remains idempotent", async () => {
  const { hooks } = loadOrb();
  const context = new hooks.RunnerContext("cleanup test");
  const trace = [];
  context.own(() => trace.push("first"));
  context.own(() => {
    trace.push("throws");
    throw new Error("cleanup boom");
  });
  context.own(() => trace.push("last"));

  const failures = await context.cleanup();
  assert.deepEqual(trace, ["last", "throws", "first"]);
  assert.equal(failures.length, 1);
  assert.equal((await context.cleanup()).length, 1);
  assert.throws(() => context.own(() => {}), /Cannot register cleanup/);
});

test("an aborted retry removes its pending timer and does not make another request", async () => {
  const timers = heldTimers();
  let requests = 0;
  const api = {
    get: async () => {
      requests += 1;
      return { status: 429, body: { retry_after: 1 } };
    }
  };
  const { hooks } = loadOrb({ api, timers });
  const context = new hooks.RunnerContext("retry abort");
  const pending = hooks.createDiscordReadAdapter(context).fetchQuests();

  await new Promise(resolve => setImmediate(resolve));
  assert.equal(requests, 1);
  assert.equal(timers.pendingCount(), 1);
  context.abort("abort retry wait");
  await assert.rejects(pending, error => error.name === "OrbAbortError");
  assert.equal(timers.pendingCount(), 0);
  assert.equal(requests, 1);
});

test("stop wins when an in-flight client read fails after cancellation", async () => {
  let rejectRequest;
  const api = {
    get: () => new Promise((resolve, reject) => { rejectRequest = reject; })
  };
  const { hooks } = loadOrb({ api });
  const context = new hooks.RunnerContext("in-flight abort");
  const pending = hooks.createDiscordReadAdapter(context).fetchQuests();

  await new Promise(resolve => setImmediate(resolve));
  context.abort("cancel while request is in flight");
  rejectRequest(new Error("late client failure"));
  await assert.rejects(pending, error => error.name === "OrbAbortError");
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

test("concurrent reads deduplicate and a successful response is cached", async () => {
  let requests = 0;
  let resolveResponse;
  const api = {
    get: () => {
      requests += 1;
      return new Promise(resolve => { resolveResponse = resolve; });
    }
  };
  const { hooks } = loadOrb({ api });
  const adapter = hooks.createDiscordReadAdapter(new hooks.RunnerContext("cache test"));
  const first = adapter.fetchQuests();
  const second = adapter.fetchQuests();
  assert.equal(requests, 1);

  resolveResponse({ status: 200, body: { quests: [] } });
  assert.deepEqual(Array.from(await first), []);
  assert.deepEqual(Array.from(await second), []);
  await adapter.fetchQuests();
  assert.equal(requests, 1);
  const refresh = adapter.fetchQuests({ forceRefresh: true });
  assert.equal(requests, 2);
  resolveResponse({ status: 200, body: { quests: [] } });
  await refresh;
});

test("a malformed API response fails clearly and releases the global run", async () => {
  const api = { get: async () => ({ status: 200, body: { quests: {} } }) };
  const { orb } = loadOrb({ api });

  await assert.rejects(orb.inspect(), /invalid quests field/);
  assert.equal(orb.status().active, false);
  const rerun = orb.mock();
  await orb.stop("post-failure restart");
  assert.equal(await rerun, null);
});

test("empty quest lists inspect cleanly", async () => {
  const api = { get: async () => ({ status: 200, body: { quests: [] } }) };
  const { orb } = loadOrb({ api });

  assert.deepEqual(Array.from(await orb.inspect()), []);
  assert.equal(orb.status().active, false);
});

test("malformed quest entries are summarized with diagnostics rather than mutating input", async () => {
  const malformed = [null, {}, "not-a-quest"];
  const api = { get: async () => ({ status: 200, body: { quests: malformed } }) };
  const { orb } = loadOrb({ api });
  const summary = await orb.inspect();

  assert.equal(summary.length, 3);
  assert.ok(summary.every(entry => entry.diagnostics.length > 0));
  assert.deepEqual(malformed, [null, {}, "not-a-quest"]);
});

test("unknown task types and unexpected config versions are visible in diagnostics", () => {
  const { hooks } = loadOrb();
  const unknownTask = hooks.summarizeQuest(quest({ taskName: "FUTURE_TASK" }));
  assert.equal(unknownTask.task.recognized, false);
  assert.match(unknownTask.diagnostics.join(" "), /Unrecognized task type/);

  const drifted = quest();
  delete drifted.config.task_config_v2;
  drifted.config.task_config_v3 = { tasks: { FUTURE_TASK: { target: 1 } } };
  const summary = hooks.summarizeQuest(drifted);
  assert.equal(summary.task, null);
  assert.match(summary.diagnostics.join(" "), /Unsupported task config version/);
});

test("expired and completed quests are never reported as active", () => {
  const { hooks } = loadOrb();
  const expired = hooks.summarizeQuest(quest({ expiresAt: new Date(Date.now() - 1_000).toISOString() }));
  const completed = hooks.summarizeQuest(quest({ completed: true }));

  assert.equal(expired.active, false);
  assert.equal(completed.active, false);
});

test("missing webpack and API-module failures are descriptive and leave no active run", async () => {
  const missingWebpack = loadOrb();
  delete missingWebpack.window.webpackChunkdiscord_app;
  await assert.rejects(missingWebpack.orb.inspect(), /webpack chunk array was not found/);
  assert.equal(missingWebpack.orb.status().active, false);

  const missingApi = loadOrb();
  await assert.rejects(missingApi.orb.inspect(), /Discord API client was not found/);
  assert.equal(missingApi.orb.status().active, false);
});
