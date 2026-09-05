(() => {
  "use strict";

  const VERSION = "2.1.0-research";
  const TAG = "[0rb:v2]";
  const CACHE_TTL_MS = 5_000;
  const MAX_429_RETRIES = 8;
  const MIN_RETRY_MS = 250;
  const MAX_RETRY_MS = 60_000;
  const KNOWN_TASK_NAMES = new Set([
    "WATCH_VIDEO", "WATCH_VIDEO_ON_MOBILE", "PLAY_ON_DESKTOP",
    "STREAM_ON_DESKTOP", "PLAY_ACTIVITY"
  ]);

  const log = (...args) => console.log(TAG, ...args);
  const warn = (...args) => console.warn(TAG, ...args);
  const isRecord = value => value !== null && typeof value === "object" && !Array.isArray(value);

  function asDisplayText(value, fallback = null) {
    return typeof value === "string" || typeof value === "number" ? String(value) : fallback;
  }

  function asFiniteNumber(value, label, diagnostics, fallback = null) {
    if (value == null || value === "") return fallback;
    const number = typeof value === "number" ? value : Number(value);
    if (Number.isFinite(number)) return number;
    diagnostics.push(`${label} is not a finite number.`);
    return fallback;
  }

  class OrbAbortError extends Error {
    constructor(message = "Orb v2 run aborted") {
      super(message);
      this.name = "OrbAbortError";
    }
  }

  // Lifecycle ownership is intentionally independent of Discord internals.
  class RunnerContext {
    constructor(label = "run") {
      this.label = label;
      this.controller = new AbortController();
      this.cleanups = [];
      this.cleanupPromise = null;
      this.cleanupFailures = [];
      this.startedAt = Date.now();
    }

    get signal() { return this.controller.signal; }
    get cleaned() { return this.cleanupPromise !== null; }

    throwIfAborted() {
      if (this.signal.aborted) throw new OrbAbortError(String(this.signal.reason ?? "aborted"));
    }

    abort(reason = "manual stop") {
      if (!this.signal.aborted) this.controller.abort(reason);
    }

    own(cleanup) {
      if (typeof cleanup !== "function") throw new TypeError("cleanup must be a function");
      if (this.cleaned) {
        throw new Error("Cannot register cleanup after RunnerContext cleanup has started.");
      }
      this.cleanups.push(cleanup);
      return cleanup;
    }

    async sleep(milliseconds) {
      if (!Number.isFinite(milliseconds) || milliseconds < 0) {
        throw new RangeError("sleep duration must be a non-negative finite number");
      }
      this.throwIfAborted();

      await new Promise((resolve, reject) => {
        let timer = null;
        let settled = false;
        const settle = (callback, value) => {
          if (settled) return;
          settled = true;
          this.signal.removeEventListener("abort", onAbort);
          if (timer !== null) clearTimeout(timer);
          callback(value);
        };
        const onAbort = () => settle(reject, new OrbAbortError(String(this.signal.reason ?? "aborted")));

        this.signal.addEventListener("abort", onAbort, { once: true });
        if (this.signal.aborted) return onAbort();
        timer = setTimeout(() => settle(resolve), milliseconds);
        if (settled) clearTimeout(timer);
      });
    }

    cleanup() {
      if (this.cleanupPromise) return this.cleanupPromise;
      this.cleanupPromise = (async () => {
        const failures = [];
        while (this.cleanups.length) {
          const cleanup = this.cleanups.pop();
          try {
            await cleanup();
          } catch (error) {
            failures.push(error);
          }
        }
        this.cleanupFailures = failures;
        if (failures.length) warn(`Cleanup completed with ${failures.length} failure(s).`, failures);
        return failures;
      })();
      return this.cleanupPromise;
    }
  }

  // Discord boundary: webpack discovery and HTTP reads are contained here.
  function getWebpackRequire() {
    const chunk = window.webpackChunkdiscord_app;
    if (!Array.isArray(chunk)) {
      throw new Error("Discord webpack chunk array was not found. This Discord build is unsupported.");
    }

    const originalLength = chunk.length;
    let capturedRequire = null;
    const token = `orb-v2-${Math.random().toString(36).slice(2)}`;
    try {
      chunk.push([[token], {}, require => { capturedRequire = require; }]);
    } catch (error) {
      throw new Error("Discord webpack require capture failed.", { cause: error });
    } finally {
      if (chunk.length > originalLength) chunk.pop();
    }

    if (!isRecord(capturedRequire?.c)) {
      throw new Error("Discord webpack require cache was unavailable after capture.");
    }
    return capturedRequire;
  }

  function candidateExports(moduleExports) {
    if (moduleExports == null || (typeof moduleExports !== "object" && typeof moduleExports !== "function")) {
      return [];
    }
    const candidates = [moduleExports];
    for (const key of ["default", "A", "Ay", "Z", "ZP", "Bo", "h"]) {
      try {
        const candidate = moduleExports[key];
        if (candidate && !candidates.includes(candidate)) candidates.push(candidate);
      } catch {
        // Lazy/proxy values occur in webpack exports; skip only this candidate.
      }
    }
    return candidates;
  }

  function findDiscordCapability(name, predicate) {
    const wpRequire = getWebpackRequire();
    let scanned = 0;
    for (const module of Object.values(wpRequire.c)) {
      for (const candidate of candidateExports(module?.exports)) {
        scanned += 1;
        try {
          if (predicate(candidate)) return candidate;
        } catch {
          // A capability check must tolerate unusual lazy exports.
        }
      }
    }
    throw new Error(
      `Discord ${name} was not found after scanning ${scanned} export candidate(s). ` +
      "The Discord client bundle may have changed."
    );
  }

  function readRateLimitDelay(source) {
    const retryAfter = source?.body?.retry_after ?? source?.response?.body?.retry_after;
    const seconds = Number(retryAfter ?? 5);
    const milliseconds = Number.isFinite(seconds) ? Math.ceil(seconds * 1_000) + 500 : 5_500;
    return Math.min(MAX_RETRY_MS, Math.max(MIN_RETRY_MS, milliseconds));
  }

  function responseStatus(source) {
    return source?.status ?? source?.response?.status;
  }

  function parseQuestResponse(response) {
    if (!isRecord(response) || !Number.isInteger(response.status)) {
      throw new Error("Quest read returned an invalid response object; expected an integer HTTP status.");
    }
    if (response.status >= 400) throw new Error(`Quest read failed: HTTP ${response.status}`);
    if (!isRecord(response.body)) {
      throw new Error("Quest read returned an invalid body; expected an object containing quests.");
    }
    if (!Array.isArray(response.body.quests)) {
      throw new Error("Quest read returned an invalid quests field; expected an array.");
    }
    return response.body.quests;
  }

  function createDiscordReadAdapter(context) {
    let api = null;
    let questCache = null;
    let questCacheExpiresAt = 0;
    let inFlight = null;

    function getApi() {
      if (!api) {
        api = findDiscordCapability("API client", candidate => typeof candidate?.get === "function");
      }
      return api;
    }

    async function fetchQuests({ forceRefresh = false } = {}) {
      context.throwIfAborted();
      if (!forceRefresh && questCache && Date.now() < questCacheExpiresAt) return questCache;
      if (inFlight) return inFlight;

      inFlight = (async () => {
        let retries = 0;
        while (true) {
          context.throwIfAborted();
          try {
            const response = await getApi().get({ url: "/quests/@me", rejectWithError: false });
            context.throwIfAborted();
            if (response?.status === 429) {
              if (retries >= MAX_429_RETRIES) throw new Error("Quest read exceeded the 429 retry budget.");
              retries += 1;
              const retryMs = readRateLimitDelay(response);
              warn(`Rate limited (${retries}/${MAX_429_RETRIES}); retrying in ${retryMs}ms.`);
              await context.sleep(retryMs);
              continue;
            }

            const quests = parseQuestResponse(response);
            context.throwIfAborted();
            questCache = quests;
            questCacheExpiresAt = Date.now() + CACHE_TTL_MS;
            return quests;
          } catch (error) {
            if (error instanceof OrbAbortError) throw error;
            // If an in-flight client request settles after stop(), cancellation wins.
            context.throwIfAborted();
            if (responseStatus(error) === 429 && retries < MAX_429_RETRIES) {
              retries += 1;
              const retryMs = readRateLimitDelay(error);
              warn(`Rate limited (${retries}/${MAX_429_RETRIES}); retrying in ${retryMs}ms.`);
              await context.sleep(retryMs);
              continue;
            }
            throw error;
          }
        }
      })();

      try {
        return await inFlight;
      } finally {
        inFlight = null;
      }
    }

    return Object.freeze({ fetchQuests });
  }

  // Parsing boundary: summaries never mutate input and retain shape diagnostics.
  function readTaskConfig(quest, diagnostics) {
    const config = quest?.config;
    if (!isRecord(config)) {
      diagnostics.push("Quest config is missing or not an object.");
      return null;
    }

    for (const key of ["task_config_v2", "task_config"]) {
      const taskConfig = config[key];
      if (taskConfig == null) continue;
      if (!isRecord(taskConfig) || !isRecord(taskConfig.tasks)) {
        diagnostics.push(`${key} is present but has no task object.`);
        continue;
      }
      const taskNames = Object.keys(taskConfig.tasks);
      if (!taskNames.length) {
        diagnostics.push(`${key} contains no tasks.`);
        continue;
      }
      return { source: key, tasks: taskConfig.tasks, taskNames };
    }

    const unexpectedVersions = Object.keys(config).filter(key => /^task_config_v\d+$/.test(key));
    diagnostics.push(
      unexpectedVersions.length
        ? `Unsupported task config version(s): ${unexpectedVersions.join(", ")}.`
        : "Quest has no supported task config."
    );
    return null;
  }

  function getTaskDetails(quest, diagnostics) {
    const taskConfig = readTaskConfig(quest, diagnostics);
    if (!taskConfig) return null;

    const [taskName] = taskConfig.taskNames;
    if (taskConfig.taskNames.length > 1) diagnostics.push(`Multiple tasks found; displaying ${taskName}.`);
    if (!KNOWN_TASK_NAMES.has(taskName)) diagnostics.push(`Unrecognized task type: ${taskName}.`);

    const task = taskConfig.tasks[taskName];
    if (!isRecord(task)) diagnostics.push(`Task ${taskName} is not an object.`);
    const progressEntry = quest?.user_status?.progress?.[taskName];
    const progressSource = typeof progressEntry === "number" ? progressEntry : progressEntry?.value;
    return {
      taskName,
      configSource: taskConfig.source,
      recognized: KNOWN_TASK_NAMES.has(taskName),
      target: asFiniteNumber(task?.target, `Target for ${taskName}`, diagnostics, null),
      progress: asFiniteNumber(progressSource, `Progress for ${taskName}`, diagnostics, 0)
    };
  }

  function isAcceptedActiveQuest(quest, now, diagnostics) {
    const enrolled = quest?.user_status?.enrolled_at != null;
    const unfinished = quest?.user_status?.completed_at == null;
    const expiryValue = quest?.config?.expires_at;
    const expiry = Date.parse(typeof expiryValue === "string" ? expiryValue : "");
    if (!Number.isFinite(expiry)) diagnostics.push("Quest expiry is missing or invalid.");
    return enrolled && unfinished && Number.isFinite(expiry) && expiry > now;
  }

  function summarizeQuest(quest, now = Date.now()) {
    const diagnostics = [];
    if (!isRecord(quest)) diagnostics.push("Quest entry is not an object.");
    const details = getTaskDetails(quest, diagnostics);
    const active = isAcceptedActiveQuest(quest, now, diagnostics);
    const id = asDisplayText(quest?.id);
    return {
      id,
      name: asDisplayText(quest?.config?.messages?.quest_name, id ?? "Unknown quest"),
      application: asDisplayText(quest?.config?.application?.name),
      expiresAt: asDisplayText(quest?.config?.expires_at),
      accepted: quest?.user_status?.enrolled_at != null,
      completed: quest?.user_status?.completed_at != null,
      active,
      task: details,
      diagnostics
    };
  }

  async function inspectLive(context) {
    const adapter = createDiscordReadAdapter(context);
    const quests = await adapter.fetchQuests({ forceRefresh: true });
    context.throwIfAborted();
    const summary = quests.map(quest => summarizeQuest(quest));
    const acceptedActive = summary.filter(quest => quest.active);
    const diagnostics = summary.flatMap(quest => quest.diagnostics.map(message => ({ id: quest.id, message })));

    log(`Found ${quests.length} quest(s); ${acceptedActive.length} accepted + active.`);
    console.table(acceptedActive.map(quest => ({
      id: quest.id,
      name: quest.name,
      task: quest.task?.taskName ?? "unknown",
      progress: quest.task?.progress ?? 0,
      target: quest.task?.target ?? "unknown",
      expiresAt: quest.expiresAt
    })));
    if (diagnostics.length) warn("Quest structures needing review:", diagnostics);
    return summary;
  }

  // Local mock: validates ownership and cancellation without accessing Discord.
  function createMockQuest(now = Date.now()) {
    return {
      id: "mock-quest-001",
      config: {
        expires_at: new Date(now + 60_000).toISOString(),
        messages: { quest_name: "Orb v2 lifecycle test" },
        application: { name: "Mock Application" },
        task_config_v2: { tasks: { MOCK_TASK: { target: 3 } } }
      },
      user_status: {
        enrolled_at: new Date(now - 1_000).toISOString(),
        completed_at: null,
        progress: { MOCK_TASK: { value: 0 } }
      }
    };
  }

  async function runMock(context) {
    const cleanupTrace = [];
    context.own(() => cleanupTrace.push("restore-store"));
    context.own(() => cleanupTrace.push("unsubscribe-listener"));
    try {
      const mockQuest = createMockQuest();
      log("Mock quest:", summarizeQuest(mockQuest));
      for (let progress = 1; progress <= 3; progress += 1) {
        context.throwIfAborted();
        await context.sleep(150);
        mockQuest.user_status.progress.MOCK_TASK.value = progress;
        log(`Mock progress ${progress}/3`);
      }
      mockQuest.user_status.completed_at = new Date().toISOString();
      return { summary: summarizeQuest(mockQuest), cleanupTrace };
    } finally {
      // executeRun calls cleanup again; RunnerContext makes that safe.
      await context.cleanup();
    }
  }

  // Public boundary: one context owns the process-wide V2 run at any time.
  let activeContext = null;
  let activePromise = null;

  async function executeRun(context, mode) {
    let result;
    let runError = null;
    try {
      log(`Orb v2 ${VERSION} started in ${mode} mode.`);
      if (mode === "inspect") result = await inspectLive(context);
      else if (mode === "mock") result = await runMock(context);
      else throw new Error(`Unknown mode: ${mode}`);
    } catch (error) {
      runError = error;
    }

    const cleanupFailures = await context.cleanup();
    if (runError instanceof OrbAbortError && !cleanupFailures.length) {
      warn("Run aborted:", runError.message);
      return null;
    }
    if (runError && cleanupFailures.length) {
      throw new AggregateError([runError, ...cleanupFailures], "Orb run failed and cleanup also failed.");
    }
    if (runError) throw runError;
    if (cleanupFailures.length) {
      throw new AggregateError(cleanupFailures, "Orb run completed but cleanup failed.");
    }
    return result;
  }

  async function run(options = {}) {
    const mode = options.mode ?? "inspect";
    if (activeContext) throw new Error("Orb v2 is already running. Call orbV2.stop() first.");

    const context = new RunnerContext(mode);
    activeContext = context;
    activePromise = (async () => {
      try {
        return await executeRun(context, mode);
      } finally {
        if (activeContext === context) {
          activeContext = null;
          activePromise = null;
        }
        log("Orb v2 run finished and cleanup completed.");
      }
    })();
    return activePromise;
  }

  async function stop(reason = "manual stop") {
    const context = activeContext;
    const promise = activePromise;
    if (!context || !promise) {
      log("No Orb v2 run is active.");
      return false;
    }
    context.abort(reason);
    await promise;
    return true;
  }

  function status() {
    return {
      version: VERSION,
      active: Boolean(activeContext),
      mode: activeContext?.label ?? null,
      startedAt: activeContext?.startedAt ?? null,
      aborted: activeContext?.signal?.aborted ?? false,
      cleanupFailures: activeContext?.cleanupFailures?.length ?? 0
    };
  }

  window.orbV2 = Object.freeze({
    version: VERSION,
    inspect: () => run({ mode: "inspect" }),
    mock: () => run({ mode: "mock" }),
    status,
    stop
  });

  // Injected only by the isolated Node harness before loading this script.
  const testHooks = window.__orbV2TestHooks;
  if (isRecord(testHooks)) {
    Object.assign(testHooks, {
      OrbAbortError,
      RunnerContext,
      createDiscordReadAdapter,
      createMockQuest,
      getWebpackRequire,
      summarizeQuest
    });
  }

  log(`Loaded Orb v2 ${VERSION}.`);
  log("Use orbV2.inspect(), orbV2.mock(), orbV2.status(), or orbV2.stop().");
})();
