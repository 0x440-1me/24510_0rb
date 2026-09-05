(() => {
  "use strict";

  const VERSION = "2.0.0-research";
  const TAG = "[0rb:v2]";

  const log = (...args) => console.log(TAG, ...args);
  const warn = (...args) => console.warn(TAG, ...args);
  const errorLog = (...args) => console.error(TAG, ...args);

  class OrbAbortError extends Error {
    constructor(message = "Orb v2 run aborted") {
      super(message);
      this.name = "OrbAbortError";
    }
  }

  class RunnerContext {
    constructor(label = "run") {
      this.label = label;
      this.controller = new AbortController();
      this.cleanups = [];
      this.cleaned = false;
      this.startedAt = Date.now();
    }

    get signal() {
      return this.controller.signal;
    }

    throwIfAborted() {
      if (this.signal.aborted) {
        throw new OrbAbortError(String(this.signal.reason ?? "aborted"));
      }
    }

    abort(reason = "manual stop") {
      if (!this.signal.aborted) {
        this.controller.abort(reason);
      }
    }

    own(cleanup) {
      if (typeof cleanup !== "function") {
        throw new TypeError("cleanup must be a function");
      }

      if (this.cleaned) {
        try {
          cleanup();
        } catch (error) {
          errorLog("Late cleanup failed:", error);
        }
        return cleanup;
      }

      this.cleanups.push(cleanup);
      return cleanup;
    }

    async sleep(milliseconds) {
      this.throwIfAborted();

      await new Promise((resolve, reject) => {
        let timer = null;

        const onAbort = () => {
          if (timer !== null) clearTimeout(timer);
          this.signal.removeEventListener("abort", onAbort);
          reject(new OrbAbortError(String(this.signal.reason ?? "aborted")));
        };

        timer = setTimeout(() => {
          this.signal.removeEventListener("abort", onAbort);
          resolve();
        }, milliseconds);

        this.signal.addEventListener("abort", onAbort, { once: true });
      });
    }

    async cleanup() {
      if (this.cleaned) return;
      this.cleaned = true;

      const failures = [];

      for (let index = this.cleanups.length - 1; index >= 0; index -= 1) {
        try {
          await this.cleanups[index]();
        } catch (error) {
          failures.push(error);
        }
      }

      this.cleanups.length = 0;

      if (failures.length) {
        warn(`Cleanup completed with ${failures.length} failure(s).`, failures);
      }
    }
  }

  function getWebpackRequire() {
    const chunk = window.webpackChunkdiscord_app;

    if (!Array.isArray(chunk)) {
      throw new Error("Discord webpack chunk array was not found.");
    }

    let capturedRequire = null;
    const token = `orb-v2-${Math.random().toString(36).slice(2)}`;

    chunk.push([
      [token],
      {},
      require => {
        capturedRequire = require;
      }
    ]);

    chunk.pop();

    if (!capturedRequire?.c) {
      throw new Error("Could not capture Discord webpack require cache.");
    }

    return capturedRequire;
  }

  function candidateExports(moduleExports) {
    if (!moduleExports) return [];

    const candidates = [moduleExports];

    for (const key of ["default", "A", "Ay", "Z", "ZP", "Bo", "h"]) {
      const candidate = moduleExports?.[key];
      if (candidate && !candidates.includes(candidate)) {
        candidates.push(candidate);
      }
    }

    return candidates;
  }

  function findExport(name, predicate) {
    const wpRequire = getWebpackRequire();

    for (const module of Object.values(wpRequire.c)) {
      for (const candidate of candidateExports(module?.exports)) {
        try {
          if (predicate(candidate)) {
            return candidate;
          }
        } catch {
          // Ignore weird lazy/proxy exports and continue scanning.
        }
      }
    }

    throw new Error(`Could not find ${name}`);
  }

  function createDiscordReadAdapter(context) {
    let api = null;
    let questCache = null;
    let questCacheExpiresAt = 0;
    let inFlight = null;

    const CACHE_TTL_MS = 5000;
    const MAX_429_RETRIES = 8;

    function getApi() {
      if (!api) {
        api = findExport(
          "Discord API client",
          candidate => typeof candidate?.get === "function"
        );
      }

      return api;
    }

    async function fetchQuests({ forceRefresh = false } = {}) {
      context.throwIfAborted();

      if (!forceRefresh && questCache && Date.now() < questCacheExpiresAt) {
        return questCache;
      }

      if (inFlight) return inFlight;

      inFlight = (async () => {
        let retries = 0;

        while (true) {
          context.throwIfAborted();

          try {
            const response = await getApi().get({
              url: "/quests/@me",
              rejectWithError: false
            });

            if (response?.status === 429) {
              if (retries >= MAX_429_RETRIES) {
                throw new Error("Quest fetch exceeded the 429 retry budget.");
              }

              retries += 1;
              const retrySeconds = Number(response?.body?.retry_after ?? 5);
              const retryMs = Math.max(250, Math.ceil(retrySeconds * 1000) + 500);

              warn(`Rate limited (${retries}/${MAX_429_RETRIES}); retrying in ${retryMs}ms.`);
              await context.sleep(retryMs);
              continue;
            }

            if (response?.status >= 400) {
              throw new Error(`Quest request failed: HTTP ${response.status}`);
            }

            const quests = Array.isArray(response?.body?.quests)
              ? response.body.quests
              : [];

            questCache = quests;
            questCacheExpiresAt = Date.now() + CACHE_TTL_MS;
            return quests;
          } catch (error) {
            if (error instanceof OrbAbortError) throw error;

            const status = error?.status ?? error?.response?.status;

            if (status === 429 && retries < MAX_429_RETRIES) {
              retries += 1;
              const retrySeconds = Number(
                error?.body?.retry_after ??
                error?.response?.body?.retry_after ??
                5
              );
              const retryMs = Math.max(250, Math.ceil(retrySeconds * 1000) + 500);

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

  function getTaskConfig(quest) {
    const candidates = [
      quest?.config?.task_config_v2,
      quest?.config?.task_config
    ];

    return candidates.find(config => config?.tasks && Object.keys(config.tasks).length)
      ?? candidates.find(Boolean)
      ?? null;
  }

  function getTaskDetails(quest) {
    const taskConfig = getTaskConfig(quest);
    const tasks = taskConfig?.tasks ?? {};
    const taskName = Object.keys(tasks)[0] ?? null;

    if (!taskName) return null;

    const progressEntry = quest?.user_status?.progress?.[taskName];
    const progress = typeof progressEntry === "number"
      ? progressEntry
      : Number(progressEntry?.value ?? 0);

    return {
      taskName,
      target: Number(tasks?.[taskName]?.target ?? 0),
      progress: Number.isFinite(progress) ? progress : 0
    };
  }

  function isAcceptedActiveQuest(quest, now = Date.now()) {
    const enrolled = quest?.user_status?.enrolled_at != null;
    const unfinished = quest?.user_status?.completed_at == null;
    const expiry = Date.parse(quest?.config?.expires_at ?? "");
    const active = Number.isFinite(expiry) && expiry > now;

    return enrolled && unfinished && active;
  }

  function summarizeQuest(quest) {
    const details = getTaskDetails(quest);

    return {
      id: quest?.id ?? null,
      name: quest?.config?.messages?.quest_name ?? quest?.id ?? "Unknown quest",
      application: quest?.config?.application?.name ?? null,
      expiresAt: quest?.config?.expires_at ?? null,
      accepted: quest?.user_status?.enrolled_at != null,
      completed: quest?.user_status?.completed_at != null,
      active: isAcceptedActiveQuest(quest),
      task: details
    };
  }

  async function inspectLive(context) {
    const adapter = createDiscordReadAdapter(context);
    const quests = await adapter.fetchQuests({ forceRefresh: true });
    context.throwIfAborted();

    const summary = quests.map(summarizeQuest);
    const acceptedActive = summary.filter(quest => quest.active);

    log(`Found ${quests.length} quest(s); ${acceptedActive.length} accepted + active.`);
    console.table(acceptedActive.map(quest => ({
      id: quest.id,
      name: quest.name,
      task: quest.task?.taskName ?? "unknown",
      progress: quest.task?.progress ?? 0,
      target: quest.task?.target ?? 0,
      expiresAt: quest.expiresAt
    })));

    return summary;
  }

  async function runMock(context) {
    const cleanupTrace = [];

    context.own(() => cleanupTrace.push("restore-store"));
    context.own(() => cleanupTrace.push("unsubscribe-listener"));

    const now = Date.now();
    const mockQuest = {
      id: "mock-quest-001",
      config: {
        expires_at: new Date(now + 60_000).toISOString(),
        messages: { quest_name: "Orb v2 lifecycle test" },
        application: { name: "Mock Application" },
        task_config_v2: {
          tasks: {
            MOCK_TASK: { target: 3 }
          }
        }
      },
      user_status: {
        enrolled_at: new Date(now - 1_000).toISOString(),
        completed_at: null,
        progress: {
          MOCK_TASK: { value: 0 }
        }
      }
    };

    log("Mock quest:", summarizeQuest(mockQuest));

    for (let progress = 1; progress <= 3; progress += 1) {
      context.throwIfAborted();
      await context.sleep(150);
      mockQuest.user_status.progress.MOCK_TASK.value = progress;
      log(`Mock progress ${progress}/3`);
    }

    mockQuest.user_status.completed_at = new Date().toISOString();
    log("Mock quest completed; cleanup will run in LIFO order.");

    return { summary: summarizeQuest(mockQuest), cleanupTrace };
  }

  let activeContext = null;
  let activePromise = null;

  async function stop(reason = "manual stop") {
    if (!activeContext) {
      log("No Orb v2 run is active.");
      return false;
    }

    activeContext.abort(reason);

    try {
      await activePromise;
    } catch (error) {
      if (!(error instanceof OrbAbortError)) throw error;
    }

    return true;
  }

  async function run(options = {}) {
    const mode = options.mode ?? "inspect";

    if (activeContext) {
      throw new Error("Orb v2 is already running. Call orbV2.stop() first.");
    }

    const context = new RunnerContext(mode);
    activeContext = context;

    activePromise = (async () => {
      try {
        log(`Orb v2 ${VERSION} started in ${mode} mode.`);

        if (mode === "inspect") {
          return await inspectLive(context);
        }

        if (mode === "mock") {
          return await runMock(context);
        }

        throw new Error(`Unknown mode: ${mode}`);
      } catch (error) {
        if (error instanceof OrbAbortError) {
          warn("Run aborted:", error.message);
          return null;
        }

        throw error;
      } finally {
        await context.cleanup();

        if (activeContext === context) {
          activeContext = null;
          activePromise = null;
        }

        log("Orb v2 run finished and cleanup completed.");
      }
    })();

    return await activePromise;
  }

  function status() {
    return {
      version: VERSION,
      active: Boolean(activeContext),
      mode: activeContext?.label ?? null,
      startedAt: activeContext?.startedAt ?? null,
      aborted: activeContext?.signal?.aborted ?? false
    };
  }

  window.orbV2 = Object.freeze({
    version: VERSION,
    run,
    stop,
    status,
    inspect: () => run({ mode: "inspect" }),
    mock: () => run({ mode: "mock" })
  });

  log(`Loaded Orb v2 ${VERSION}.`);
  log("Use orbV2.inspect(), orbV2.mock(), orbV2.status(), or orbV2.stop().");
})();
